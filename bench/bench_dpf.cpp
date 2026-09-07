// ==========================================================================
//  bench_dpf.cpp -- cost of the DPF, per operation, both rings.
//
//  JSONL to stdout, human-readable table to stderr. The Makefile does the
//  redirect, so this binary has no file I/O and no cwd assumption.
//
//  ------------------------------------------------------------------------
//  WHAT EACH ROW ANSWERS
//
//    gen         client per-query cost. bytes_sent is one key's serialised
//                size, which is what one server actually receives, so the
//                key-size table falls out of these rows rather than needing
//                a row type of its own.
//    eval        the FSS gate cost. Makes "logarithmic in the domain" a
//                measured line rather than an assertion.
//    evalfull    the server's PIR cost. Decides whether DPF-PIR is viable at
//                the catalogue sizes this project actually uses.
//    eval_domain 2^d independent Evals. The ratio against evalfull is the
//                result that justifies EvalFull existing at all.
//    serialize / deserialize   the only per-query allocation on the wire path.
//    csprng32    a bare RandomBytes draw, to isolate whether small-domain Gen
//                is dominated by two BCryptGenRandom calls rather than by the
//                tree. If it is, that is itself a finding.
//
//  ------------------------------------------------------------------------
//  KEEPING THE OPTIMISER HONEST
//
//  The primary defence is structural: Gen, Eval and EvalFull live in a
//  separate translation unit and there is no -flto, so GCC cannot see the
//  bodies and cannot elide the calls. That stops being true the moment anyone
//  adds LTO, so on top of it every result is accumulated into a live value and
//  sunk, and the checksum is printed. A checksum that changes between runs is
//  also evidence the CSPRNG is genuinely being drawn.
//
//  The EvalFull output buffer is allocated ONCE, outside every loop. A fresh
//  8 MB zero-initialised vector per call would time the page-fault handler
//  rather than the DPF, which is the easiest way to ship a wrong number here.
// ==========================================================================
#include "oblivrec/bench.hpp"
#include "oblivrec/csprng.hpp"
#include "oblivrec/dpf.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

// Domain sizes: 8 and 10 for the shape of the curve, 11 because ML-100K's
// 1682 items pad to 2048 and that is what the demo runs, 12 for ML-1M, then
// 14 and 16 to show the logarithmic key growth. 20 caps EvalFull at 8 MB of
// output; dpf.cpp asserts 24, which is 128 MB and not a workload.
const std::uint32_t kDomains[] = {8, 10, 11, 12, 14, 16};
constexpr int kReps = 5;

template <typename Ring>
Row BaseRow(std::uint32_t db, const char* phase, const char* op) {
  Row r;
  r.profile = "local";
  r.stage = "S1";
  r.phase = phase;
  // d is the EMBEDDING dimension in this schema and no embedding is involved
  // here, so it stays null rather than being overloaded with the tree depth.
  r.d = Opt<long long>::None();
  // n is the number of indexable slots, which for a DPF is the padded domain.
  r.n = static_cast<long long>(1u << db);
  r.b = RingTraits<Ring>::kBits;
  // domain_bits is NOT derivable from n in general: a real PIR row will carry
  // n = 1682 with domain_bits = 11, because the catalogue pads. So it gets its
  // own field.
  r.Extra("domain_bits", static_cast<long long>(db));
  r.ExtraStr("op", op);
  r.ExtraStr("ring", RingTraits<Ring>::Name());
  return r;
}

template <typename Ring>
void BenchRing(Writer& w, std::vector<Ring>& out_buf) {
  std::fprintf(stderr, "  ring %s\n", RingTraits<Ring>::Name());
  std::fprintf(stderr, "    %4s %8s %12s %12s %12s %10s\n",
               "bits", "n", "gen us", "eval us", "evalfull us", "key B");

  for (std::uint32_t db : kDomains) {
    const std::size_t n = std::size_t(1) << db;
    const std::uint32_t mask = (1u << db) - 1u;

    // Query indices precomputed outside the timed region. An in-loop LCG
    // would time the LCG, and a stride-1 sweep would give the branch
    // predictor an unrealistically easy job on the tree path.
    std::vector<std::uint32_t> xs(1024);
    {
      std::vector<std::uint8_t> raw(xs.size() * 4);
      RandomBytes(raw.data(), raw.size());
      for (std::size_t i = 0; i < xs.size(); ++i) {
        std::uint32_t v = 0;
        std::memcpy(&v, &raw[i * 4], 4);
        xs[i] = v & mask;
      }
    }

    auto kp = Gen<Ring>(xs[0], Ring(1), db);
    const auto ser = kp.first.Serialize();
    const long long key_bytes = static_cast<long long>(kp.first.SizeBytes());

    Ring acc = 0;
    double gen_us = 0, eval_us = 0, full_us = 0;

    // ---- Gen -------------------------------------------------------------
    {
      Row r = BaseRow<Ring>(db, "fss", "gen");
      r.bytes_sent = key_bytes;   // what one server receives
      std::size_t i = 0;
      gen_us = 1000.0 * TimeAndEmit(w, r, kReps, [&] {
        auto p = Gen<Ring>(xs[i++ & 1023], Ring(1), db);
        acc = static_cast<Ring>(acc + p.first.cw_last);
        ClobberMemory();
      });
    }

    // ---- Eval ------------------------------------------------------------
    {
      Row r = BaseRow<Ring>(db, "fss", "eval");
      std::size_t i = 0;
      eval_us = 1000.0 * TimeAndEmit(w, r, kReps, [&] {
        acc = static_cast<Ring>(acc + Eval<Ring>(kp.first, xs[i++ & 1023]));
      });
    }

    // ---- EvalFull --------------------------------------------------------
    {
      Row r = BaseRow<Ring>(db, "pir", "evalfull");
      full_us = 1000.0 * TimeAndEmit(w, r, kReps, [&] {
        EvalFull<Ring>(kp.first, Span<Ring>(out_buf.data(), n));
        acc = static_cast<Ring>(acc + out_buf[0]);
      }, 50.0);
    }

    // ---- 2^d independent Evals, for the ratio against EvalFull ------------
    // Only up to 12 bits: at 16 this is 65536 Evals per iteration and the
    // ratio is already established by then.
    if (db <= 12) {
      Row r = BaseRow<Ring>(db, "fss", "eval_domain");
      TimeAndEmit(w, r, kReps, [&] {
        Ring a = 0;
        for (std::uint32_t x = 0; x < (1u << db); ++x)
          a = static_cast<Ring>(a + Eval<Ring>(kp.first, x));
        acc = static_cast<Ring>(acc + a);
      }, 50.0);
    }

    // ---- Serialize / Deserialize -----------------------------------------
    {
      Row r = BaseRow<Ring>(db, "fss", "serialize");
      r.bytes_sent = key_bytes;
      TimeAndEmit(w, r, kReps, [&] {
        auto v = kp.first.Serialize();
        Sink(v);
      });
    }
    {
      Row r = BaseRow<Ring>(db, "fss", "deserialize");
      r.bytes_sent = key_bytes;
      TimeAndEmit(w, r, kReps, [&] {
        auto k = DpfKey<Ring>::Deserialize(
            Span<const std::uint8_t>(ser.data(), ser.size()));
        acc = static_cast<Ring>(acc + k.cw_last);
      });
    }

    Sink(acc);
    std::fprintf(stderr, "    %4u %8zu %12.3f %12.3f %12.1f %10lld\n",
                 db, n, gen_us, eval_us, full_us, key_bytes);
  }
}

}  // namespace

int main() {
  Writer w(stdout);
  std::fprintf(stderr, "bench_dpf  git_sha=%s\n", OBLIVREC_GIT_SHA);

  // Allocated once at the largest domain used, so no timed region ever pays
  // for a first-touch page fault.
  std::size_t max_n = 0;
  for (std::uint32_t db : kDomains) max_n = std::max<std::size_t>(max_n, std::size_t(1) << db);
  std::vector<u64> buf64(max_n);
  std::vector<u128> buf128(max_n);

  // The CSPRNG on its own, to see how much of a small-domain Gen is just two
  // BCryptGenRandom calls.
  {
    Row r;
    r.phase = "setup";
    r.stage = "S1";
    r.ExtraStr("op", "csprng32");
    std::uint8_t tmp[32];
    TimeAndEmit(w, r, kReps, [&] {
      RandomBytes(tmp, sizeof(tmp));
      Sink(tmp);
    });
  }

  BenchRing<u64>(w, buf64);
  BenchRing<u128>(w, buf128);

  std::fprintf(stderr, "done\n");
  return 0;
}
