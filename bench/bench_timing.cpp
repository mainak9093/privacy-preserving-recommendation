// ==========================================================================
//  bench_timing.cpp -- is PirServer::Answer's runtime independent of WHICH
//  record was asked for?
//
//  docs/threat-model.md has carried this as an open item since Phase 2: frame
//  SIZES are shown constant -- 228 bytes over 1800 queries, with a classifier
//  on the raw bytes at chance -- but inter-frame TIMING was never analysed,
//  and adversary class A6 explicitly still excludes it.
//
//  ------------------------------------------------------------------------
//  WHAT IS MEASURED HERE, AND WHY IT IS THE ANSWERABLE HALF.
//
//  Inter-frame timing ON THE WIRE is dominated by the OS scheduler, the TCP
//  stack and the loopback interface. Measuring it would mostly measure
//  Windows. What this project actually controls, and what a timing attack
//  would have to exploit, is whether the SERVER'S WORK depends on alpha.
//
//  It should not. EvalFull walks every node of the GGM tree and the answer
//  takes an unconditional inner product over every record -- there is no
//  data-dependent branch and no data-dependent memory pattern by design,
//  because any shortcut would leak the index. So this is a test of a property
//  the construction claims, not an exploration.
//
//  ------------------------------------------------------------------------
//  THE RESULT IS A BOUND, NOT A ZERO, AND THAT IS DELIBERATE.
//
//  No finite sample can show a difference is exactly zero. What it can do is
//  bound how large a difference could hide under the measurement noise. This
//  binary emits raw per-call timings per alpha; the analysis reports the
//  spread BETWEEN alphas against the spread WITHIN one alpha, which is the
//  comparison that matters. If between-alpha variation is no larger than
//  within-alpha variation, any data-dependent signal is below our noise floor,
//  and the honest claim is that bound rather than "constant time".
//
//  ARCHITECTURE section 10 records that this machine drifts up to 4x between
//  runs thermally while within-run spread is 1.1-1.7x. So the alphas are
//  INTERLEAVED rather than measured in blocks: block order would let drift
//  align with alpha identity and manufacture exactly the signal we are testing
//  for. Same reasoning as bench_compose.cpp's rep-major loop.
// ==========================================================================
#include "bench_common.hpp"

#include "oblivrec/bench.hpp"
#include "oblivrec/catalogue.hpp"
#include "oblivrec/pir.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

constexpr int kSamples = 200;        // per alpha, interleaved

}  // namespace

int main() {
  Writer w(stdout);

  std::unique_ptr<Catalogue> cat_p;
  try {
    cat_p.reset(new Catalogue(Catalogue::LoadMovieLens("data/ml-100k/u.item")));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "bench_timing: %s\n", e.what());
    return 0;                        // emit nothing rather than invent rows
  }
  const Catalogue& cat = *cat_p;

  PirServer<u64> server(cat);
  PirClient<u64> client(cat.DomainBits());
  const std::size_t W = RecordWords<u64>();
  const std::uint32_t items = cat.NumItems();

  // Alphas spanning the domain: the ends, the middle, and a few in between.
  // If Answer's cost depended on the index at all -- through tree structure,
  // cache behaviour or anything else -- these are where it would show.
  const std::uint32_t alphas[] = {0, 1, items / 4, items / 2,
                                  3 * items / 4, items - 2, items - 1, 7};
  constexpr std::size_t kNAlpha = sizeof(alphas) / sizeof(alphas[0]);

  // Keys are generated ONCE, outside the timed region. Timing Gen as well
  // would measure the client, and the question is about the server.
  std::vector<std::pair<DpfKey<u64>, DpfKey<u64>>> keys;
  keys.reserve(kNAlpha);
  for (std::size_t i = 0; i < kNAlpha; ++i) {
    keys.push_back(client.Query(alphas[i]));
  }

  std::vector<u64> out(W);

  // Warm up: first touch of the catalogue and the output buffer should not
  // land inside a measured sample.
  for (std::size_t i = 0; i < kNAlpha; ++i) {
    server.Answer(keys[i].first, Span<u64>(out.data(), out.size()));
  }
  Sink(out[0]);

  std::fprintf(stderr,
               "timing side channel: %d samples x %zu alphas, INTERLEAVED\n",
               kSamples, kNAlpha);

  for (int s = 0; s < kSamples; ++s) {
    for (std::size_t i = 0; i < kNAlpha; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      server.Answer(keys[i].first, Span<u64>(out.data(), out.size()));
      const double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0).count();
      Sink(out[0]);

      Row r;
      r.stage = "S1";
      r.phase = "pir";
      r.n = items;
      r.b = 64;
      r.k = 1;
      r.wall_ms = ms;
      r.bytes_sent = static_cast<long long>(W * sizeof(u64));
      r.ExtraStr("op", "answer_timing");
      r.Extra("alpha", static_cast<long long>(alphas[i]));
      r.Extra("sample", static_cast<long long>(s));
      r.Extra("domain_bits", static_cast<long long>(cat.DomainBits()));
      w.Emit(r);
    }
  }

  std::fprintf(stderr,
               "  raw per-call timings emitted; the between-alpha against "
               "within-alpha\n  comparison is done in bench/scripts/"
               "make_figures.py, which reports a BOUND\n  rather than a claim "
               "of constant time.\n");
  return 0;
}
