// ==========================================================================
//  bench_baseline.cpp -- what a private read costs, against what it replaces.
//
//  REQUIREMENTS task 2.10 asks for the B1 and B5 baselines. A private-read
//  number in isolation means nothing; it is only interpretable between the two
//  things a reader would otherwise propose:
//
//    B1  cleartext lookup.       No privacy. Send the index, get the record.
//                                The FLOOR. Its job is to price privacy
//                                honestly, and the ratio is not flattering.
//    B5  full-catalogue download. Perfect privacy by triviality: ask for
//                                everything and filter at home. The CEILING,
//                                and the one a reader proposes first, because
//                                at 430 KB it is not obviously unreasonable.
//    ..  DPF-PIR.                What we built. One EvalFull plus an
//                                unconditional inner product, per server.
//
//  ------------------------------------------------------------------------
//  WHY THE PHASE IS "pir" AND NOT "baseline".
//
//  ARCHITECTURE section 10 fixes the phase enum, and bench.hpp ABORTS on a
//  phase outside it, deliberately, so a typo cannot silently produce rows the
//  plot script drops. Adding "baseline" would mean amending section 10, which
//  RULES requires be done before the code, not alongside it.
//
//  It would also be the wrong model. These three rows measure the SAME
//  operation -- retrieve one record -- by three methods. That is exactly what
//  the `op` extra distinguishes, and `op` is already established here
//  (bench_dpf.cpp emits "evalfull" and "csprng32"). So phase stays "pir" and
//  the three methods are three ops, which is also what makes them joinable.
//
//  ------------------------------------------------------------------------
//  NOTE ON THE EXISTING pir ROWS. bench_dpf.cpp's phase="pir" rows carry
//  op="evalfull" and measure key expansion ALONE, with bytes_sent = 0. They
//  are not the end-to-end answer cost and must not be read as such. This
//  binary measures PirServer::Answer, which is EvalFull *plus* the inner
//  product over 32 ring words per record -- the whole of what a server does.
//
//  Absolute timings on this machine vary up to 4x between back-to-back runs
//  (thermal), while within-run spread is 1.1-1.7x. Read the RATIOS between
//  these three ops, which are taken in one run and are therefore comparable;
//  do not quote the absolutes across runs.
// ==========================================================================
#include "oblivrec/bench.hpp"
#include "oblivrec/catalogue.hpp"
#include "oblivrec/dpf.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec/span.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

constexpr int kReps = 10;

void RunAll(Writer& w, const Catalogue& cat) {
  const std::uint32_t n = cat.NumItems();
  const std::uint32_t db = cat.DomainBits();
  const std::size_t L = Catalogue::kRecordBytes;
  const std::size_t W = RecordWords<u64>();

  Row proto;
  proto.stage = "S1";
  proto.phase = "pir";
  proto.n = static_cast<long long>(n);
  proto.b = 64;

  std::fprintf(stderr, "catalogue: %u items, domain %u (%u bits), %zu B records\n",
               n, cat.DomainSize(), db, L);
  std::fprintf(stderr, "\n  %-18s %12s %14s %12s\n",
               "method", "bytes/query", "median ms", "vs cleartext");

  // ---- B1: cleartext lookup ---------------------------------------------
  // The server is told the index and copies the record out. No privacy at all.
  // bytes = 4 (the index, in the clear) + L (the record).
  std::vector<std::uint8_t> sink(L);
  std::uint32_t which = 0;
  double b1_median = 1.0;   // set by the B1 block; the ratios below divide by it
  {
    Row r = proto;
    r.ExtraStr("op", "b1_cleartext");
    r.Extra("domain_bits", static_cast<long long>(db));
    r.bytes_sent = static_cast<long long>(4 + L);
    const double med = TimeAndEmit(w, r, kReps, [&] {
      which = (which + 1) & (cat.DomainSize() - 1);
      auto rec = cat.Record(which);
      std::memcpy(sink.data(), &rec[0], L);
      Sink(sink[0]);
    });
    std::fprintf(stderr, "  %-18s %12lld %14.6f %12s\n",
                 "B1 cleartext", static_cast<long long>(4 + L), med, "1.00x");
    b1_median = med;
  }

  // ---- B5: full catalogue download --------------------------------------
  // Perfect privacy, because the server learns nothing beyond "someone asked".
  // The cost is the entire catalogue, every time.
  std::vector<std::uint8_t> whole(std::size_t(cat.DomainSize()) * L);
  {
    Row r = proto;
    r.ExtraStr("op", "b5_fulldownload");
    r.Extra("domain_bits", static_cast<long long>(db));
    r.bytes_sent = static_cast<long long>(std::size_t(n) * L);
    const double med = TimeAndEmit(w, r, kReps, [&] {
      for (std::uint32_t j = 0; j < cat.DomainSize(); ++j) {
        auto rec = cat.Record(j);
        std::memcpy(&whole[std::size_t(j) * L], &rec[0], L);
      }
      Sink(whole[0]);
    });
    std::fprintf(stderr, "  %-18s %12lld %14.6f %11.1fx\n",
                 "B5 full download", static_cast<long long>(std::size_t(n) * L),
                 med, med / b1_median);
  }

  // ---- DPF-PIR: one server's answer -------------------------------------
  // EvalFull over the whole domain, then an unconditional inner product across
  // W ring words. The client sends one key and receives W words.
  {
    PirServer<u64> server(cat);
    PirClient<u64> client(db);
    auto keys = client.Query(0);
    const std::size_t key_bytes = keys.first.SizeBytes();
    std::vector<u64> answer(W);

    Row r = proto;
    r.ExtraStr("op", "dpf_pir_answer");
    r.Extra("domain_bits", static_cast<long long>(db));
    r.Extra("key_bytes", static_cast<long long>(key_bytes));
    // What ONE server sees: the key up, the answer down. The client talks to
    // two servers, so the client-side total is twice this.
    r.bytes_sent = static_cast<long long>(key_bytes + W * sizeof(u64));
    const double med = TimeAndEmit(w, r, kReps, [&] {
      server.Answer(keys.first, Span<u64>(answer.data(), W));
      Sink(answer[0]);
    });
    std::fprintf(stderr, "  %-18s %12lld %14.6f %11.1fx\n",
                 "DPF-PIR (1 srv)",
                 static_cast<long long>(key_bytes + W * sizeof(u64)),
                 med, med / b1_median);
    std::fprintf(stderr, "\n  DPF-PIR sends %.0fx fewer bytes than B5 and costs\n"
                 "  %.0fx more time than B1. That trade is the whole point.\n",
                 double(std::size_t(n) * L) / double(key_bytes + W * sizeof(u64)),
                 med / b1_median);
  }
}

}  // namespace

int main() {
  try {
    Catalogue cat = Catalogue::LoadMovieLens("data/ml-100k/u.item");
    Writer w(stdout);
    RunAll(w, cat);
  } catch (const std::exception& e) {
    // data/ is gitignored, so a fresh clone legitimately has no catalogue.
    // Emit nothing rather than invent rows; the plot script skips the figure
    // and says so.
    std::fprintf(stderr,
                 "bench_baseline: no catalogue (%s).\n"
                 "data/ml-100k is gitignored; run scripts/fetch_data.py.\n"
                 "Emitting no rows rather than inventing any.\n", e.what());
    return 0;
  }
  return 0;
}
