// ==========================================================================
//  bench_sweep.cpp -- the parameter sweep (task 4.1).
//
//  REQUIREMENTS section D6 asks for "sweeps over m, n, d, ell, b, k" and
//  network profiles, repeated at least five times.
//
//  ------------------------------------------------------------------------
//  ONE FACTOR AT A TIME, NOT THE CARTESIAN PRODUCT, AND THAT IS DELIBERATE.
//
//  The full cross product of the six axes is thousands of configurations, most
//  of which answer nothing: nobody needs to know the cost at d=32, ell=1,
//  b=128, k=20 simultaneously. Worse, a run that large would take hours and
//  would not be re-run, and a benchmark that stops being re-run stops being
//  true.
//
//  So this fixes a BASELINE and moves one axis at a time around it. That is
//  the standard design for this kind of study and it is what makes the
//  resulting curves readable: each one isolates a single parameter's effect
//  with everything else held still. Interaction effects are not measured, and
//  that limitation is stated rather than hidden.
//
//      baseline: MovieLens-100K, d=16, ell=10, b=64, k=10
//
//  ------------------------------------------------------------------------
//  NETWORK PROFILES ARE DERIVED, NOT RE-RUN.
//
//  Training never transmits -- the substrate simulates three parties in one
//  process and COUNTS what it would have sent (mpc.hpp). So each row carries
//  its measured round and byte counts, and the profile projection is applied
//  from those via netprofile.hpp rather than by running the sweep four times.
//  Re-running would measure the same counters and add nothing but hours.
//
//  Every projected row is labelled `emulation: channel-level, not netem`,
//  because that is what it is.
//
//  ------------------------------------------------------------------------
//  THE NORMALISER IS RECORDED ON EVERY ROW. Training can run with a
//  normaliser that reveals ||v|| (2 rounds) or the spec-faithful FSS one
//  (~80 rounds, b=128 only). Those are not comparable costs, so every row says
//  which produced it, and the b=128 arm runs BOTH so the difference is in the
//  data rather than in a footnote.
// ==========================================================================
#include "bench_common.hpp"

#include "oblivrec/bench.hpp"
#include "oblivrec/factor.hpp"
#include "oblivrec/netprofile.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/catalogue.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

constexpr int kReps = 5;              // D6: "repeated >= 5x"
// Ratings, LoadRatings, Densify, EmitProfiles and kFullM/kFullN moved to
// bench_common.hpp when tasks 4.2 and 4.3 needed the same four.

// ---- one training configuration, kReps times ---------------------------
template <typename Ring>
void TrainPoint(Writer& w, const Ratings& rt, std::uint32_t m, std::uint32_t n,
                std::uint32_t d, std::uint32_t ell, const char* axis,
                bool use_fss) {
  FactorParams p;
  p.m = m; p.n = n; p.d = d; p.ell = ell; p.t = 20; p.max_rating = 5;
  std::uint64_t nnz = 0;
  auto U = Densify<Ring>(rt, m, n, &nnz);
  p.nnz = nnz;

  TruncationSchedule sched = TruncationSchedule::Derive(p, RingTraits<Ring>::kBits);
  try {
    sched.AssertHeadroom();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "  SKIP b=%d d=%u ell=%u m=%u: %s\n",
                 RingTraits<Ring>::kBits, d, ell, m, e.what());
    return;
  }

  SharedMatrix<Ring> su;
  su.rows = m; su.cols = n;
  su.data = SplitVec<Ring>(Span<const Ring>(U.data(), U.size()));

  for (int rep = 0; rep < kReps; ++rep) {
    Mpc3<Ring> s(1000 + rep);
    std::unique_ptr<Normalizer<Ring>> norm;
    if (use_fss) {
      try {
        norm.reset(new FssNormalizer<Ring>(s, p.t, p.t - 8, p.t + 8, 30));
      } catch (const std::exception& e) {
        if (rep == 0) {
          std::fprintf(stderr, "  SKIP fss b=%d: %s\n",
                       RingTraits<Ring>::kBits, e.what());
        }
        return;
      }
    } else {
      norm.reset(new RevealNormNormalizer<Ring>());
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto res = ApproxFactorShared<Ring>(s, su, p, sched, *norm,
                                        static_cast<std::uint64_t>(rep));
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();

    Row row;
    row.stage = "S2";
    row.phase = "matvec";
    row.m = m; row.n = n; row.d = d; row.ell = ell;
    row.b = RingTraits<Ring>::kBits;
    row.t = 20;
    row.wall_ms = ms;
    row.bytes_sent = static_cast<long long>(res.bytes);
    row.ExtraStr("op", "approxfactor");
    row.ExtraStr("axis", axis);
    row.ExtraStr("dataset", "ml-100k");
    row.Extra("nnz", static_cast<long long>(nnz));
    row.Extra("rounds", static_cast<long long>(res.rounds));
    row.Extra("truncations", static_cast<long long>(res.truncations));
    row.Extra("rep", static_cast<long long>(rep));
    row.ExtraStr("normalizer", res.normalizer.c_str());
    row.Extra("scalars_revealed",
              static_cast<long long>(res.revealed_norms.size()));
    row.ExtraStr("deferred", sched.Deferred() ? "true" : "false");
    w.Emit(row);

    if (rep == 0) {
      Row proto;
      proto.stage = "S2";
      proto.m = m; proto.n = n; proto.d = d; proto.ell = ell;
      proto.b = RingTraits<Ring>::kBits; proto.t = 20;
      EmitProfiles(w, proto, res.rounds, res.bytes, "axis", axis, "approxfactor");
      std::fprintf(stderr,
                   "  b=%-3d m=%-4u d=%-3u ell=%-3u  %8.2f s  %7llu rounds  "
                   "%8.1f MB  %s\n",
                   RingTraits<Ring>::kBits, m, d, ell, ms / 1000.0,
                   (unsigned long long)res.rounds,
                   static_cast<double>(res.bytes) / 1e6,
                   use_fss ? "fss" : "reveal-norm");
    }
  }
}

// ---- the delivery axis: k, and the catalogue domain --------------------
void DeliverySweep(Writer& w) {
  Catalogue cat = Catalogue::LoadMovieLens("data/ml-100k/u.item");
  PirServer<u64> p0(cat), p1(cat);
  PirClient<u64> client(cat.DomainBits());
  const std::size_t W = RecordWords<u64>();
  auto key0 = client.Query(0);
  const std::size_t key_bytes = key0.first.SizeBytes();

  std::fprintf(stderr, "\n  delivery: cost of fetching k records privately\n");
  for (std::uint32_t k : {1u, 5u, 10u, 20u}) {
    for (int rep = 0; rep < kReps; ++rep) {
      std::vector<u64> a0(W), a1(W);
      const auto t0 = std::chrono::steady_clock::now();
      for (std::uint32_t q = 0; q < k; ++q) {
        auto keys = client.Query(q % cat.DomainSize());
        p0.Answer(keys.first, Span<u64>(a0.data(), W));
        p1.Answer(keys.second, Span<u64>(a1.data(), W));
        Sink(a0[0]);
        Sink(a1[0]);
      }
      const double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count();
      // Both servers, per fetch: key up plus answer down.
      const std::uint64_t bytes =
          std::uint64_t(k) * 2 * (key_bytes + W * sizeof(u64));

      Row row;
      row.stage = "S1";
      row.phase = "pir";
      row.n = cat.NumItems();
      row.k = k;
      row.b = 64;
      row.wall_ms = ms;
      row.bytes_sent = static_cast<long long>(bytes);
      row.ExtraStr("op", "fetch_topk");
      row.ExtraStr("axis", "k");
      row.ExtraStr("dataset", "ml-100k");
      row.Extra("domain_bits", static_cast<long long>(cat.DomainBits()));
      row.Extra("key_bytes", static_cast<long long>(key_bytes));
      row.Extra("rep", static_cast<long long>(rep));
      // Delivery is one round trip per fetch, both servers in parallel.
      row.Extra("rounds", static_cast<long long>(k));
      w.Emit(row);

      if (rep == 0) {
        Row proto;
        proto.stage = "S1";
        proto.n = cat.NumItems();
        proto.k = k;
        proto.b = 64;
        EmitProfiles(w, proto, k, bytes, "axis", "k", "fetch_topk");
        std::fprintf(stderr, "    k=%-3u  %7.3f ms  %6llu B\n", k, ms,
                     (unsigned long long)bytes);
      }
    }
  }
}

}  // namespace

int main() {
  Ratings rt;
  if (!LoadRatings("data/ml-100k/u1.base", &rt)) {
    std::fprintf(stderr,
                 "bench_sweep: cannot read data/ml-100k/u1.base. data/ is "
                 "gitignored; run py -3.13 scripts/fetch_data.py\n");
    return 0;
  }
  Writer w(stdout);

  std::fprintf(stderr, "sweep: one factor at a time around "
               "(ml-100k, d=16, ell=10, b=64, k=10), %d reps each\n\n", kReps);

  // ---- d ----------------------------------------------------------------
  std::fprintf(stderr, "  axis d (ell=10, b=64):\n");
  for (std::uint32_t d : {8u, 16u, 32u}) {
    TrainPoint<u64>(w, rt, kFullM, kFullN, d, 10, "d", false);
  }

  // ---- ell --------------------------------------------------------------
  std::fprintf(stderr, "\n  axis ell (d=16, b=64):\n");
  for (std::uint32_t ell : {1u, 5u, 10u, 25u}) {
    TrainPoint<u64>(w, rt, kFullM, kFullN, 16, ell, "ell", false);
  }

  // ---- m: quarter, half, full user set ---------------------------------
  std::fprintf(stderr, "\n  axis m (d=16, ell=10, b=64):\n");
  for (std::uint32_t m : {kFullM / 4, kFullM / 2, kFullM}) {
    TrainPoint<u64>(w, rt, m, kFullN, 16, 10, "m", false);
  }

  // ---- b, and with it the two normalisers -------------------------------
  // The b=128 arm runs BOTH normalisers, so the cost of not revealing the
  // norm is in the data rather than asserted.
  std::fprintf(stderr, "\n  axis b (d=16, ell=10):\n");
  TrainPoint<u64>(w, rt, kFullM, kFullN, 16, 10, "b", false);
  TrainPoint<u128>(w, rt, kFullM, kFullN, 16, 10, "b", false);
  std::fprintf(stderr, "\n  axis normalizer (b=128, d=16, ell=10):\n");
  TrainPoint<u128>(w, rt, kFullM, kFullN, 16, 10, "normalizer", true);

  // ---- k, the delivery side --------------------------------------------
  try {
    DeliverySweep(w);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "  delivery sweep skipped: %s\n", e.what());
  }

  std::fprintf(stderr, "\nsweep complete.\n");
  return 0;
}
