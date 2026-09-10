// ==========================================================================
//  train.cpp -- private training on real data.
//
//      train --ell 10 --d 16                 (task 3.7, the convergence study)
//      train --headroom-only                 (task 3.8, the D9.1 scale study)
//
//  Two jobs, because the two tasks have very different costs.
//
//  3.7 runs ApproxFactor under secret sharing on MovieLens-100K for a range of
//  ell, writes B, and emits JSONL. Quality (nDCG@20) is scored in Python
//  against the same metric model/metrics.py already uses for the cleartext
//  oracle, so the two numbers are comparable by construction rather than by
//  assertion.
//
//  3.8 is the scale question, and the honest answer at ML-1M is analytic
//  rather than empirical -- see the note in the headroom section below.
//
//  WHAT THIS DOES NOT CLAIM. The normalisation step is RevealNormNormalizer,
//  which opens one scalar per call. That is not what section 5 specifies and
//  it changes the leakage profile; every run records how many scalars it
//  revealed, and the JSONL carries the normaliser's name, so no number
//  produced here can be quoted without it.
// ==========================================================================
#include "oblivrec/factor.hpp"
#include "oblivrec/netprofile.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

struct Dataset {
  std::vector<u64> U;      // m x n, scale 0
  std::uint32_t m = 0, n = 0;
  std::uint64_t nnz = 0;
};

// MovieLens ratings: "user<TAB>item<TAB>rating<TAB>timestamp", 1-based ids.
bool LoadRatings(const std::string& path, std::uint32_t m, std::uint32_t n,
                 Dataset* out) {
  std::ifstream f(path);
  if (!f) return false;
  out->m = m;
  out->n = n;
  out->U.assign(std::size_t(m) * n, u64(0));
  long u, it, r, ts;
  while (f >> u >> it >> r >> ts) {
    if (u >= 1 && u <= (long)m && it >= 1 && it <= (long)n) {
      out->U[std::size_t(u - 1) * n + (it - 1)] = static_cast<u64>(r);
      ++out->nnz;
    }
  }
  return out->nnz > 0;
}

std::string Now() {
  const std::time_t t = std::time(nullptr);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
  return buf;
}

// ------------------------------------------------------------------------
//  Task 3.8: where does b = 64 stop working?
//
//  This is answered from PUBLIC parameters alone -- m, n, nnz, d, t -- so it
//  needs no data and no simulation, which is what makes it answerable at
//  scales the simulation itself cannot reach.
//
//  It is worth being explicit about why ML-1M is not run end to end here.
//  Sharing a dense 6040 x 3706 matrix costs 22.4M ring elements, which at 16
//  bytes per replicated share across three parties is ~1.1 GB, and the
//  transpose doubles it. The simulation holds all three parties in one
//  process, so that is 2.1 GB before any arithmetic, on a machine whose commit
//  limit has already been exhausted three times during this work. NUDGE ran
//  its Netflix-scale experiments on 3 x 192 cores; stating our hardware and
//  our scale plainly is what the risk register asks for, rather than quietly
//  reporting a smaller experiment as if it were the intended one.
// ------------------------------------------------------------------------
void HeadroomStudy(std::FILE* jsonl) {
  struct Scale { const char* name; std::uint32_t m, n; std::uint64_t nnz; };
  const Scale scales[] = {
      {"ml-100k", 943, 1682, 100000},
      {"ml-1m", 6040, 3706, 1000209},
      {"netflix-ish", 480189, 17770, 100480507},
  };
  const std::uint32_t ts[] = {12, 16, 20, 24};
  const int rings[] = {64, 128};

  std::printf("\n  D9.1 -- ring width against scale (schedule feasibility)\n");
  std::printf("  %-12s %-4s %-4s %-10s %-9s %s\n", "dataset", "b", "t",
              "schedule", "headroom", "verdict");

  for (const auto& sc : scales) {
    for (int b : rings) {
      for (std::uint32_t t : ts) {
        FactorParams p;
        p.m = sc.m; p.n = sc.n; p.nnz = sc.nnz;
        p.d = 16; p.ell = 10; p.t = t; p.max_rating = 5;
        auto sch = TruncationSchedule::Derive(p, b);

        bool ok = true;
        try { sch.AssertHeadroom(); } catch (const std::exception&) { ok = false; }
        std::printf("  %-12s %-4d %-4u %-10s %-9d %s\n", sc.name, b, t,
                    sch.Deferred() ? "deferred" : "immediate",
                    sch.HeadroomBits(), ok ? "ok" : "TOO NARROW");

        if (jsonl) {
          std::fprintf(jsonl,
                       "{\"git_sha\": \"%s\", \"profile\": \"local\", "
                       "\"stage\": \"S2\", \"phase\": \"setup\", "
                       "\"op\": \"headroom\", \"dataset\": \"%s\", "
                       "\"m\": %u, \"n\": %u, \"nnz\": %llu, \"d\": %u, "
                       "\"t\": %u, \"b\": %d, \"deferred\": %s, "
                       "\"growth_bits\": %d, \"peak_bits\": %u, "
                       "\"headroom_bits\": %d, \"feasible\": %s, "
                       "\"wall_ms\": 0, \"bytes_sent\": 0, "
                       "\"timestamp\": \"%s\"}\n",
                       OBLIVREC_GIT_SHA, sc.name, sc.m, sc.n,
                       (unsigned long long)sc.nnz, p.d, t, b,
                       sch.Deferred() ? "true" : "false", sch.GrowthBits(),
                       sch.PeakFracBits(), sch.HeadroomBits(),
                       ok ? "true" : "false", Now().c_str());
        }
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::uint32_t d = 16, ell = 10, t = 20;
  bool headroom_only = false;
  std::string ratings = "data/ml-100k/u1.base";
  std::string out_b = "model/out/B_private.bin";
  std::string jsonl_path = "bench/results/train.jsonl";

  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--d") && i + 1 < argc) d = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--ell") && i + 1 < argc) ell = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--t") && i + 1 < argc) t = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--ratings") && i + 1 < argc) ratings = argv[++i];
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out_b = argv[++i];
    else if (!std::strcmp(argv[i], "--headroom-only")) headroom_only = true;
    else {
      std::fprintf(stderr,
                   "usage: train [--d N] [--ell N] [--t N] [--ratings F] "
                   "[--out F] [--headroom-only]\n");
      return 2;
    }
  }

  std::FILE* jsonl = std::fopen(jsonl_path.c_str(), "a");

  if (headroom_only) {
    HeadroomStudy(jsonl);
    if (jsonl) std::fclose(jsonl);
    return 0;
  }

  Dataset ds;
  if (!LoadRatings(ratings, 943, 1682, &ds)) {
    std::fprintf(stderr,
                 "train: cannot read %s. data/ is gitignored; run "
                 "py -3.13 scripts/fetch_data.py\n", ratings.c_str());
    if (jsonl) std::fclose(jsonl);
    return 1;
  }

  FactorParams p;
  p.m = ds.m; p.n = ds.n; p.nnz = ds.nnz;
  p.d = d; p.ell = ell; p.t = t; p.max_rating = 5;

  auto sched = TruncationSchedule::Derive(p, 64);
  std::printf("train: %ux%u, %llu ratings, d=%u ell=%u t=%u\n", p.m, p.n,
              (unsigned long long)p.nnz, d, ell, t);
  std::printf("  schedule: %s\n", sched.Explain().c_str());
  try {
    sched.AssertHeadroom();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "train: %s\n", e.what());
    if (jsonl) std::fclose(jsonl);
    return 1;
  }

  SharedMatrix<u64> su;
  su.rows = p.m;
  su.cols = p.n;
  su.data = SplitVec<u64>(Span<const u64>(ds.U.data(), ds.U.size()));

  Mpc3<u64> s(20260911);
  RevealNormNormalizer<u64> rn;

  const auto t0 = std::chrono::steady_clock::now();
  auto res = ApproxFactorShared<u64>(s, su, p, sched, rn, 0);
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();

  std::printf("  done in %.1f s: %llu rounds, %llu B, %llu truncations\n",
              ms / 1000.0, (unsigned long long)res.rounds,
              (unsigned long long)res.bytes,
              (unsigned long long)res.truncations);
  std::printf("  normaliser: %s -- %zu scalars revealed\n",
              res.normalizer.c_str(), res.revealed_norms.size());

  // Task 3.10. Training never transmits -- the substrate counts what it would
  // have sent -- so the honest way to get WAN numbers is to apply the cost
  // model to those counters rather than to fake a transmission. See
  // netprofile.hpp for what this does and does not model.
  const NetProfile profiles[] = {kProfileLan, kProfileWanA, kProfileWanB};
  std::printf("\n  projected wall time by network profile "
              "(channel-level model, NOT netem):\n");
  std::printf("    %-8s %-12s %-12s %-12s %s\n", "profile", "latency s",
              "transfer s", "total s", "bound by");
  for (const auto& np : profiles) {
    const double lat = static_cast<double>(res.rounds) * np.rtt_ms;
    const double tot = PredictedMs(res.rounds, res.bytes, np);
    std::printf("    %-8s %-12.1f %-12.1f %-12.1f %s\n", np.name, lat / 1000.0,
                (tot - lat) / 1000.0, tot / 1000.0,
                Bottleneck(res.rounds, res.bytes, np));
    if (jsonl) {
      std::fprintf(jsonl,
                   "{\"git_sha\": \"%s\", \"profile\": \"%s\", "
                   "\"stage\": \"S2\", \"phase\": \"net\", "
                   "\"op\": \"approxfactor_projected\", "
                   "\"dataset\": \"ml-100k\", \"d\": %u, \"ell\": %u, "
                   "\"t\": %u, \"b\": 64, \"rounds\": %llu, "
                   "\"bytes_sent\": %llu, \"rtt_ms\": %.1f, "
                   "\"mbps\": %.1f, \"wall_ms\": %.3f, "
                   "\"bound_by\": \"%s\", \"emulation\": "
                   "\"channel-level, not netem\", \"timestamp\": \"%s\"}\n",
                   OBLIVREC_GIT_SHA, np.name, d, ell, t,
                   (unsigned long long)res.rounds,
                   (unsigned long long)res.bytes, np.rtt_ms, np.mbps, tot,
                   Bottleneck(res.rounds, res.bytes, np), Now().c_str());
    }
  }

  // B, t-scaled, little-endian int64, the same format model/export.py writes.
  {
    std::ofstream f(out_b, std::ios::binary);
    for (u64 x : res.B) {
      std::uint8_t b8[8];
      for (int i = 0; i < 8; ++i) b8[i] = static_cast<std::uint8_t>(x >> (8 * i));
      f.write(reinterpret_cast<const char*>(b8), 8);
    }
  }
  std::printf("  wrote %s (%zu elements)\n", out_b.c_str(), res.B.size());

  if (jsonl) {
    std::fprintf(jsonl,
                 "{\"git_sha\": \"%s\", \"profile\": \"local\", "
                 "\"stage\": \"S2\", \"phase\": \"matvec\", "
                 "\"op\": \"approxfactor\", \"dataset\": \"ml-100k\", "
                 "\"m\": %u, \"n\": %u, \"nnz\": %llu, \"d\": %u, \"ell\": %u, "
                 "\"t\": %u, \"b\": 64, \"deferred\": %s, "
                 "\"rounds\": %llu, \"bytes_sent\": %llu, "
                 "\"truncations\": %llu, \"normalizer\": \"%s\", "
                 "\"scalars_revealed\": %zu, \"wall_ms\": %.3f, "
                 "\"timestamp\": \"%s\"}\n",
                 OBLIVREC_GIT_SHA, p.m, p.n, (unsigned long long)p.nnz, d, ell,
                 t, sched.Deferred() ? "true" : "false",
                 (unsigned long long)res.rounds,
                 (unsigned long long)res.bytes,
                 (unsigned long long)res.truncations, res.normalizer.c_str(),
                 res.revealed_norms.size(), ms, Now().c_str());
    std::fclose(jsonl);
  }
  return 0;
}
