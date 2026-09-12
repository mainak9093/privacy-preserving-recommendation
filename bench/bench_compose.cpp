// ==========================================================================
//  bench_compose.cpp -- task 4.2, the headline comparison.
//
//  REQUIREMENTS D6: "B1-B3 are the important ones: the composition is the
//  project, so the cost of each half separately is the result."
//
//      B1    cleartext MF + cleartext fetch     the floor, and the quality oracle
//      B2    cleartext MF + DPF-PIR             isolates PRIVATE DELIVERY
//      B3    private MF   + cleartext fetch     isolates PRIVATE TRAINING (Nudge's scope)
//      FULL  private MF   + DPF-PIR             the composition -- the project
//      B5    cleartext MF + full download       trivially private, absurd bandwidth
//
//  B4 (MP-SPDZ) is out of scope with reasons recorded in
//  bench/scripts/baselines.py and the Decisions Log. It is not an omission.
//
//  ------------------------------------------------------------------------
//  ONE PROCESS, AND THE REPETITIONS ARE REP-MAJOR.
//
//  ARCHITECTURE section 10 records that absolute timings on this laptop drift
//  up to 4x between back-to-back runs while within-run spread is only
//  1.1-1.7x. Five arms timed in five processes would be measuring the thermal
//  state of the machine. So all five run here, and the loop is
//
//      for rep: for arm:            NOT       for arm: for rep:
//
//  because arm-major lets drift correlate with arm identity, which is exactly
//  the artefact the caveat warns about. Rep-major spreads it evenly. The
//  report quotes RATIOS between arms, never the absolutes.
//
//  ------------------------------------------------------------------------
//  WHAT THE CLEARTEXT ARM IS, PRECISELY.
//
//  ApproxFactorClear, not model/mf.py. It is the fixed-point twin taking the
//  SAME truncation points, so the comparison isolates the cost of secret
//  SHARING rather than conflating it with a change of numerics or of
//  language. model/mf.py remains the QUALITY oracle; these two jobs are
//  different and conflating them is the one mistake available in this arm.
//  Every training row says which it is via `quality_source`.
//
//  ------------------------------------------------------------------------
//  THE UNFLATTERING RESULT, WHICH IS WHY THIS TASK IS WORTH DOING.
//
//  Delivery is issued as k SEQUENTIAL round trips (demo.cpp fetches one
//  record at a time). On wan_a that makes DPF-PIR latency-bound at k*RTT,
//  while the full-catalogue download pays ONE round trip -- so B5 beats PIR
//  despite sending ~45x more bytes. fig_pir_cost's "PIR wins on any real
//  network" is a LOCAL-profile conclusion about bytes and CPU, and criterion
//  276 asks for wan_a precisely because conclusions flip there.
//
//  The client knows all k indices at once, so the queries could go in one
//  round trip. That variant is emitted too, as op="compose_batched" with
//  implemented="false" -- what is built and what is possible, both labelled,
//  neither quietly substituted for the other.
// ==========================================================================
#include "bench_common.hpp"

#include "oblivrec/bench.hpp"
#include "oblivrec/catalogue.hpp"
#include "oblivrec/factor.hpp"
#include "oblivrec/netprofile.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/serve.hpp"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

constexpr int kReps = 5;
constexpr std::uint32_t kD = 16, kEll = 10, kT = 20, kK = 10;

// One measured point for an arm, in a rep.
struct Half {
  double ms = 0.0;
  std::uint64_t rounds = 0, bytes = 0;
};

struct Arm {
  const char* name;            // B1 / B2 / B3 / FULL / B5
  bool private_training;
  const char* delivery;        // cleartext | pir | download
  const char* privacy;         // neither | delivery | training | both
};

const Arm kArms[] = {
    {"B1",   false, "cleartext", "neither"},
    {"B2",   false, "pir",       "delivery"},
    {"B3",   true,  "cleartext", "training"},
    {"FULL", true,  "pir",       "both"},
    {"B5",   false, "download",  "delivery"},
};

double MsSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main() {
  Writer w(stdout);

  Ratings rt;
  if (!LoadRatings("data/ml-100k/u1.base", &rt)) {
    std::fprintf(stderr,
                 "bench_compose: data/ml-100k/u1.base missing. Run "
                 "py -3.13 scripts/fetch_data.py\n");
    return 0;                        // emit nothing rather than invent rows
  }
  // Catalogue has no default constructor on purpose -- there is no such thing
  // as an empty catalogue -- so it is built inside the try, not assigned into.
  std::unique_ptr<Catalogue> cat_p;
  try {
    cat_p.reset(new Catalogue(Catalogue::LoadMovieLens("data/ml-100k/u.item")));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "bench_compose: %s\n", e.what());
    return 0;
  }
  const Catalogue& cat = *cat_p;

  const std::uint32_t m = kFullM, n = kFullN;
  const std::uint32_t items = cat.NumItems();
  const std::size_t W = RecordWords<u64>();
  const std::size_t rec_bytes = Catalogue::kRecordBytes;

  std::uint64_t nnz = 0;
  auto U = Densify<u64>(rt, m, n, &nnz);

  FactorParams p;
  p.m = m; p.n = n; p.nnz = nnz; p.d = kD; p.ell = kEll; p.t = kT;
  p.max_rating = 5;
  auto sched = TruncationSchedule::Derive(p, 64);
  try {
    sched.AssertHeadroom();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "bench_compose: %s\n", e.what());
    return 0;
  }

  PirServer<u64> p0(cat), p1(cat);
  PirClient<u64> client(cat.DomainBits());
  const std::size_t key_bytes = client.Query(0).first.SizeBytes();

  // The ten items a delivery fetches. Fixed across arms and reps so every arm
  // retrieves exactly the same records.
  std::vector<std::uint32_t> want;
  for (std::uint32_t i = 0; i < kK; ++i) want.push_back((i * 137 + 11) % items);

  // Scoring inputs, shared once: every arm scores the same way (B is public,
  // so scoring is 0 rounds regardless of how B was obtained).
  std::vector<u64> Bring(std::size_t(kD) * items, u64(1) << 18);
  std::vector<ReplicatedShare<u64>> a_share(kD);
  for (std::uint32_t i = 0; i < kD; ++i) {
    a_share[i] = ReplicatedShare<u64>(u64(1) << 18, u64(0));
  }
  auto mask = BuildMaskShares<u64>(std::vector<std::uint8_t>(items, 0), items);

  std::fprintf(stderr,
               "task 4.2 -- B1 / B2 / B3 / FULL / B5, one process, rep-major\n"
               "  ml-100k %ux%u  d=%u ell=%u b=64 k=%u, %d reps\n",
               m, n, kD, kEll, kK, kReps);

  // Medians across reps, per arm, for the composed rows.
  std::vector<std::vector<double>> train_ms(5), deliver_ms(5), topk_ms(5);
  Half train_clear, train_priv;
  std::uint64_t deliver_bytes[5] = {0, 0, 0, 0, 0};
  std::uint64_t deliver_rounds[5] = {0, 0, 0, 0, 0};

  for (int rep = 0; rep < kReps; ++rep) {
    for (std::size_t ai = 0; ai < 5; ++ai) {
      const Arm& arm = kArms[ai];

      // ---- training -----------------------------------------------------
      Half tr;
      if (arm.private_training) {
        // A fresh session per rep so the counters measure one run, and a FIXED
        // seed so the reps measure timing variance rather than model variance.
        SharedMatrix<u64> su;
        su.rows = m; su.cols = n;
        su.data = SplitVec<u64>(Span<const u64>(U.data(), U.size()));
        Mpc3<u64> s(20260913);
        RevealNormNormalizer<u64> rn;
        const auto t0 = std::chrono::steady_clock::now();
        auto res = ApproxFactorShared<u64>(s, su, p, sched, rn, 0);
        tr.ms = MsSince(t0);
        tr.rounds = res.rounds;
        tr.bytes = res.bytes;
        train_priv = tr;
      } else {
        const auto t0 = std::chrono::steady_clock::now();
        auto B = ApproxFactorClear<u64>(Span<const u64>(U.data(), U.size()), p, 0);
        tr.ms = MsSince(t0);
        Sink(B[0]);
        train_clear = tr;            // 0 rounds, 0 bytes: nothing is shared
      }
      train_ms[ai].push_back(tr.ms);

      // ---- score and select ---------------------------------------------
      std::vector<ReplicatedShare<u64>> scored(items);
      const auto ts = std::chrono::steady_clock::now();
      ScoreShares<u64>(a_share, Span<const u64>(Bring.data(), Bring.size()), kD,
                       items, mask[0], scored);
      std::vector<std::int64_t> flat(items);
      for (std::uint32_t j = 0; j < items; ++j) {
        flat[j] = static_cast<std::int64_t>(scored[j].lo);
      }
      auto top = TopK(Span<const std::int64_t>(flat.data(), flat.size()), kK);
      topk_ms[ai].push_back(MsSince(ts));
      Sink(top[0]);

      // ---- delivery ------------------------------------------------------
      Half dl;
      const auto td = std::chrono::steady_clock::now();
      if (!std::strcmp(arm.delivery, "cleartext")) {
        std::size_t acc = 0;
        for (std::uint32_t j : want) acc += cat.Record(j).size();
        Sink(acc);
        dl.bytes = std::uint64_t(kK) * (4 + rec_bytes);   // index + record
        dl.rounds = kK;                                   // one per fetch
      } else if (!std::strcmp(arm.delivery, "pir")) {
        std::vector<u64> a0(W), a1(W);
        for (std::uint32_t j : want) {
          auto keys = client.Query(j);
          p0.Answer(keys.first, Span<u64>(a0.data(), a0.size()));
          p1.Answer(keys.second, Span<u64>(a1.data(), a1.size()));
          auto rec = PirClient<u64>::ReconstructBytes(
              Span<const u64>(a0.data(), a0.size()),
              Span<const u64>(a1.data(), a1.size()));
          // The check that makes "B2 has exactly B1's quality" measured
          // rather than assumed: the bytes delivered are the bytes stored.
          if (std::memcmp(rec.data(), cat.Record(j).data(), rec_bytes) != 0) {
            std::fprintf(stderr,
                         "bench_compose: PIR returned the wrong record for "
                         "item %u. Refusing to emit rows.\n", j);
            return 1;
          }
        }
        // Per server, doubled for the client's two-server total.
        dl.bytes = std::uint64_t(kK) * 2 * (key_bytes + W * sizeof(u64));
        dl.rounds = kK;
      } else {
        std::size_t acc = 0;
        for (std::uint32_t j = 0; j < items; ++j) acc += cat.Record(j).size();
        Sink(acc);
        dl.bytes = std::uint64_t(items) * rec_bytes;
        dl.rounds = 1;               // ask once, filter at home
      }
      dl.ms = MsSince(td);
      deliver_ms[ai].push_back(dl.ms);
      deliver_bytes[ai] = dl.bytes;
      deliver_rounds[ai] = dl.rounds;

      // ---- rows ----------------------------------------------------------
      Row r;
      r.stage = "S3";
      r.phase = "matvec";
      r.m = m; r.n = n; r.d = kD; r.ell = kEll; r.b = 64; r.t = kT;
      r.wall_ms = tr.ms;
      r.bytes_sent = static_cast<long long>(tr.bytes);
      r.ExtraStr("op", arm.private_training ? "train_private" : "train_clear");
      r.ExtraStr("config", arm.name);
      r.ExtraStr("dataset", "ml-100k");
      r.Extra("nnz", static_cast<long long>(nnz));
      r.Extra("rounds", static_cast<long long>(tr.rounds));
      r.Extra("rep", static_cast<long long>(rep));
      r.ExtraStr("privacy", arm.privacy);
      r.ExtraStr("quality_source",
                 arm.private_training ? "private" : "oracle");
      r.ExtraStr("normalizer",
                 arm.private_training ? "reveal-norm (LEAKS ||v||)" : "none");
      w.Emit(r);

      Row tk;
      tk.stage = "S3";
      tk.phase = "topk";
      tk.n = items; tk.d = kD; tk.k = kK; tk.b = 64; tk.t = kT;
      tk.wall_ms = topk_ms[ai].back();
      tk.bytes_sent = 0;             // public B on shares: no communication
      tk.ExtraStr("op", "score_and_select");
      tk.ExtraStr("config", arm.name);
      tk.Extra("rounds", 0LL);
      tk.Extra("rep", static_cast<long long>(rep));
      w.Emit(tk);

      Row dr;
      dr.stage = "S3";
      dr.phase = "pir";
      dr.n = items; dr.k = kK; dr.b = 64; dr.t = kT;
      dr.wall_ms = dl.ms;
      dr.bytes_sent = static_cast<long long>(dl.bytes);
      dr.ExtraStr("op", (std::string("deliver_") + arm.delivery).c_str());
      dr.ExtraStr("config", arm.name);
      dr.Extra("rounds", static_cast<long long>(dl.rounds));
      dr.Extra("rep", static_cast<long long>(rep));
      dr.Extra("domain_bits", static_cast<long long>(cat.DomainBits()));
      dr.Extra("key_bytes",
               static_cast<long long>(std::strcmp(arm.delivery, "pir") ? 0
                                                                       : key_bytes));
      dr.ExtraStr("delivery_exact",
                  std::strcmp(arm.delivery, "pir") ? "n/a" : "true");
      dr.ExtraStr("privacy", arm.privacy);
      w.Emit(dr);
    }
    std::fprintf(stderr, "  rep %d done\n", rep);
  }

  // ---- the invariant, asserted before anything is plotted ---------------
  //
  // The halves are independent: training bytes are server-to-server, delivery
  // bytes are client-to-server. So FULL must equal B2 + B3 - B1 exactly. If it
  // does not, something is double-counted, and it is better to fail here than
  // to let a figure launder it into a result.
  auto TrainBytes = [&](std::size_t ai) {
    return kArms[ai].private_training ? train_priv.bytes : train_clear.bytes;
  };
  const std::uint64_t full = TrainBytes(3) + deliver_bytes[3];
  const std::uint64_t parts = (TrainBytes(1) + deliver_bytes[1]) +
                              (TrainBytes(2) + deliver_bytes[2]) -
                              (TrainBytes(0) + deliver_bytes[0]);
  std::fprintf(stderr,
               "\n  additivity: FULL = %llu B, B2+B3-B1 = %llu B -- %s\n",
               (unsigned long long)full, (unsigned long long)parts,
               full == parts ? "EXACT" : "*** MISMATCH ***");
  if (full != parts) {
    std::fprintf(stderr,
                 "  Composing is not the sum of its halves, which means a byte "
                 "is counted twice.\n  Refusing to emit composed rows.\n");
    return 1;
  }

  // ---- composed rows, from the medians ----------------------------------
  auto Median = [](std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };

  std::fprintf(stderr,
               "\n  %-5s %-9s %12s %10s %12s %12s\n", "arm", "privacy",
               "train ms", "deliver B", "local ms", "wan_a ms");

  for (std::size_t ai = 0; ai < 5; ++ai) {
    const Arm& arm = kArms[ai];
    const double tms = Median(train_ms[ai]);
    const double dms = Median(deliver_ms[ai]) + Median(topk_ms[ai]);

    // Training is paid once per model refresh; delivery once per session. To
    // put them in one number at all, training is amortised over the m users a
    // refresh serves. The denominator is a MODELLING CHOICE, not a
    // measurement, so both halves stay separately reported and every row
    // carries the basis.
    const std::uint64_t tr_rounds =
        arm.private_training ? train_priv.rounds : 0;
    const std::uint64_t tr_bytes = TrainBytes(ai);
    const double amort = static_cast<double>(m);

    for (int batched = 0; batched < 2; ++batched) {
      const std::uint64_t dl_rounds =
          batched ? 1 : deliver_rounds[ai];
      if (batched && deliver_rounds[ai] == 1) continue;   // already one trip

      const double rounds_f = tr_rounds / amort + static_cast<double>(dl_rounds);
      const double bytes_f = tr_bytes / amort +
                             static_cast<double>(deliver_bytes[ai]);
      const std::uint64_t rounds_i =
          static_cast<std::uint64_t>(rounds_f + 0.5);
      const std::uint64_t bytes_i = static_cast<std::uint64_t>(bytes_f + 0.5);
      const double compute_ms = tms / amort + dms;

      const NetProfile profiles[] = {kProfileLocal, kProfileLan, kProfileWanA,
                                     kProfileWanB};
      for (const auto& np : profiles) {
        Row c;
        c.stage = "S3";
        c.phase = "net";
        c.profile = np.name;
        c.m = m; c.n = n; c.d = kD; c.ell = kEll; c.b = 64; c.t = kT;
        c.k = kK;
        c.wall_ms = compute_ms + PredictedMs(rounds_i, bytes_i, np);
        c.bytes_sent = static_cast<long long>(bytes_i);
        c.ExtraStr("op", batched ? "compose_batched" : "compose");
        c.ExtraStr("config", arm.name);
        c.ExtraStr("privacy", arm.privacy);
        c.Extra("rounds", static_cast<long long>(rounds_i));
        c.Extra("rtt_ms", np.rtt_ms);
        c.Extra("mbps", np.mbps);
        c.Extra("compute_ms", compute_ms);
        c.Extra("net_ms", PredictedMs(rounds_i, bytes_i, np));
        c.Extra("train_ms", tms);
        c.Extra("deliver_ms", dms);
        c.Extra("train_bytes", static_cast<long long>(tr_bytes));
        c.Extra("deliver_bytes",
                static_cast<long long>(deliver_bytes[ai]));
        c.Extra("amort_users", static_cast<long long>(m));
        c.ExtraStr("amort_basis", "one model refresh over m users");
        c.ExtraStr("bound_by", Bottleneck(rounds_i, bytes_i, np));
        c.ExtraStr("emulation", "channel-level, not netem");
        c.ExtraStr("implemented", batched ? "false" : "true");
        c.ExtraStr("quality_source",
                   arm.private_training ? "private" : "oracle");
        w.Emit(c);

        if (!batched && !std::strcmp(np.name, "wan_a")) {
          std::fprintf(stderr, "  %-5s %-9s %12.1f %10llu %12.2f %12.2f\n",
                       arm.name, arm.privacy, tms,
                       (unsigned long long)deliver_bytes[ai],
                       compute_ms + PredictedMs(rounds_i, bytes_i, kProfileLocal),
                       c.wall_ms);
        }
      }
    }
  }

  std::fprintf(stderr,
               "\n  Delivery is k sequential round trips, so on wan_a the "
               "full download (1 trip)\n  beats DPF-PIR (%u trips) despite "
               "~45x the bytes. The batched variant is\n  emitted alongside as "
               "compose_batched, implemented=false.\n", kK);
  return 0;
}
