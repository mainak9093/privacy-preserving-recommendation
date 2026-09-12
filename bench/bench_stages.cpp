// ==========================================================================
//  bench_stages.cpp -- task 4.3, the microbenchmark breakdown.
//
//  REQUIREMENTS D6 asks for "matvec vs truncation vs normalization vs FSS vs
//  network", and success criterion 277 asks specifically that "the interactive
//  cost [be] attributed to truncation vs normalization".
//
//  ------------------------------------------------------------------------
//  WHY THE ATTRIBUTION IS ANALYTIC AND NOT INSTRUMENTED.
//
//  There is no seam around s.MatVec or TruncatePair inside ApproxFactorShared
//  -- the loop calls them directly -- so an in-situ timer would mean editing
//  protocol code three weeks before a freeze. But rounds and bytes do not need
//  instrumenting: every communicating operation in this codebase charges
//  itself through exactly two places,
//
//      Mpc3::Exchange     1 round, 3*len elements       (mpc.cpp:131-146)
//      AccountRound(e)    1 round, e     elements       (mpc.hpp:179)
//
//  and each protocol's charge is fixed and known:
//
//      TruncatePair(len)  3 rounds, 8*len   (4n helper, 2n open, 2n reshare)
//      MsnzbGate::Apply   2 rounds, 5*len   (3n open c, 2n reshare)
//      InnerProduct(len)  1 round,  3       (n products collapse to one elem)
//      MulVec(len)        1 round,  3*len
//      RevealNorm's leak  1 round,  3       (AccountRound(3), factor.cpp:166)
//      opening B[i]       1 round,  3*n     (factor.cpp:347)
//
//  So the whole cost is a closed form over (m, n, d, ell, deferred,
//  normalizer, newton_steps). That is better than instrumentation, not a
//  substitute for it: it gives rounds AND bytes per phase with no protocol
//  edit, and it is SELF-CHECKING -- the per-phase sums must equal the
//  res.rounds and res.bytes of a real run, or the model is wrong and this
//  binary says so and exits non-zero. Every row carries the residual.
//
//  ------------------------------------------------------------------------
//  WHAT NOT TO USE: FactorResult.truncations.
//
//  An earlier draft attributed truncation as `truncations * 3`. That is wrong.
//  The field counts only factor.cpp's own four increment sites; every
//  TruncatePair inside a Normalizer is invisible to it, because Normalizer
//  ::Apply has no access to the result struct. The committed sweep proves it:
//  at b=128 the field reads 325 for BOTH the reveal-norm run (2191 rounds) and
//  the FSS run (11519 rounds). A counter equal across a 5x difference in
//  rounds is not counting the thing that differs.
//
//  ------------------------------------------------------------------------
//  AND WHAT IS STILL MEASURED RATHER THAN DERIVED: wall time.
//
//  Rounds and bytes are exact; milliseconds are not derivable and are timed
//  here with TimeAndEmit, one protocol at a time. Those rows are a RATE, taken
//  out of the training loop's context and therefore out of its cache state.
//  They are not an in-situ attribution and must not be read as one. The one
//  genuine in-situ seam is Normalizer, a pure-virtual interface that
//  ApproxFactorShared takes by reference -- so CountingNormalizer below wraps
//  it with zero edits to src/, and independently confirms the analytic
//  normalize term.
// ==========================================================================
#include "bench_common.hpp"

#include "oblivrec/bench.hpp"
#include "oblivrec/catalogue.hpp"
#include "oblivrec/factor.hpp"
#include "oblivrec/msnzb.hpp"
#include "oblivrec/netprofile.hpp"
#include "oblivrec/nonlinear.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/serve.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

constexpr int kNewtonSteps = 4;      // InvSqrtShared's default
constexpr int kReps = 5;

// ---- the closed form ----------------------------------------------------
struct Budget {
  std::uint64_t rounds = 0, elems = 0;
  void Add(std::uint64_t r, std::uint64_t e) { rounds += r; elems += e; }
};

struct StageModel {
  Budget matvec, truncate, normalize, fss, open_b;

  std::uint64_t Rounds() const {
    return matvec.rounds + truncate.rounds + normalize.rounds + fss.rounds +
           open_b.rounds;
  }
  std::uint64_t Elems() const {
    return matvec.elems + truncate.elems + normalize.elems + fss.elems +
           open_b.elems;
  }
};

// TruncatePair(len): 3 rounds, 8*len elements. trunc.cpp:138-140.
inline void Trunc(Budget* b, std::uint64_t len) { b->Add(3, 8 * len); }

StageModel Model(std::uint32_t m, std::uint32_t n, std::uint32_t d,
                 std::uint32_t ell, bool deferred, bool use_fss,
                 int newton_steps) {
  StageModel s;
  for (std::uint32_t comp = 0; comp < d; ++comp) {
    const std::uint32_t filled = comp;

    // ---- orthogonalise, once before the loop and once per iteration -----
    auto Orthogonalise = [&]() {
      if (filled == 0) return;                  // nothing to project out
      if (deferred) {
        Trunc(&s.truncate, n);
      } else {
        Trunc(&s.truncate, filled);             // the batched dot products
        Trunc(&s.truncate, n);
      }
    };

    // ---- normalise ------------------------------------------------------
    auto Normalise = [&]() {
      if (!use_fss) {
        s.normalize.Add(1, 3);                  // InnerProduct -> 1 element
        s.normalize.Add(1, 3);                  // the revealed scalar
        Trunc(&s.truncate, n);                  // rescale by the public recip
        return;
      }
      s.normalize.Add(1, 3);                    // InnerProduct(v,v)
      Trunc(&s.truncate, 1);                    // ||v||^2 to scale t
      s.fss.Add(2, 5);                          // MsnzbGate::Apply, len 1
      for (int st = 0; st < newton_steps; ++st) {
        s.normalize.Add(1, 3);  Trunc(&s.truncate, 1);   // S*y
        s.normalize.Add(1, 3);  Trunc(&s.truncate, 1);   // sy*y
        s.normalize.Add(1, 3);  Trunc(&s.truncate, 1);   // y*corr, halved
      }
      s.normalize.Add(1, 3 * n);                // MulVec(v, spread)
      Trunc(&s.truncate, n);
    };

    Orthogonalise();
    Normalise();
    for (std::uint32_t it = 0; it < ell; ++it) {
      s.matvec.Add(1, 3 * m);                   // MatVec(U, v)
      s.matvec.Add(1, 3 * n);                   // MatVec(Ut, uv)
      Trunc(&s.truncate, n);                    // the post-matvec shift
      Orthogonalise();
      Normalise();
    }
    s.open_b.Add(1, 3 * n);                     // OpenVec + AccountRound(3n)
  }
  return s;
}

// ------------------------------------------------------------------------
//  The one real in-situ seam: Normalizer is a pure-virtual interface and
//  ApproxFactorShared takes it BY REFERENCE, so wrapping it needs no edit to
//  src/ at all. This is the established before/after idiom from
//  factor.cpp:303 and :350-351, applied per call.
// ------------------------------------------------------------------------
template <typename Ring>
class CountingNormalizer final : public Normalizer<Ring> {
 public:
  explicit CountingNormalizer(Normalizer<Ring>* inner) : inner_(inner) {}

  SharedVec<Ring> Apply(Mpc3<Ring>& s, const SharedVec<Ring>& v,
                        std::uint32_t t) override {
    const std::uint64_t r0 = s.Rounds(), b0 = s.BytesSent();
    const auto t0 = std::chrono::steady_clock::now();
    auto out = inner_->Apply(s, v, t);
    ms_ += std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0).count();
    rounds_ += s.Rounds() - r0;
    bytes_ += s.BytesSent() - b0;
    ++calls_;
    return out;
  }
  const char* Name() const override { return inner_->Name(); }
  bool RevealsNorm() const override { return inner_->RevealsNorm(); }
  const std::vector<double>& Revealed() const override {
    return inner_->Revealed();
  }

  std::uint64_t calls() const { return calls_; }
  std::uint64_t rounds() const { return rounds_; }
  std::uint64_t bytes() const { return bytes_; }
  double ms() const { return ms_; }

 private:
  Normalizer<Ring>* inner_;
  std::uint64_t calls_ = 0, rounds_ = 0, bytes_ = 0;
  double ms_ = 0.0;
};

// ------------------------------------------------------------------------
//  (A) + (C): validate the model against a real run, and emit the breakdown.
// ------------------------------------------------------------------------
template <typename Ring>
bool Attribute(Writer& w, const Ratings& rt, std::uint32_t d, std::uint32_t ell,
               bool use_fss, int* failures) {
  const std::uint32_t m = kFullM, n = kFullN;
  constexpr int kBits = RingTraits<Ring>::kBits;

  std::uint64_t nnz = 0;
  auto U = Densify<Ring>(rt, m, n, &nnz);

  FactorParams p;
  p.m = m; p.n = n; p.nnz = nnz; p.d = d; p.ell = ell; p.t = 20;
  p.max_rating = 5;

  auto sched = TruncationSchedule::Derive(p, kBits);
  try {
    sched.AssertHeadroom();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "  SKIP b=%d: %s\n", kBits, e.what());
    return false;
  }

  SharedMatrix<Ring> su;
  su.rows = m; su.cols = n;
  su.data = SplitVec<Ring>(Span<const Ring>(U.data(), U.size()));
  U.clear();
  U.shrink_to_fit();

  Mpc3<Ring> s(20260913);
  std::unique_ptr<Normalizer<Ring>> inner;
  if (use_fss) {
    try {
      // The SAME range src/apps/train.cpp uses. A narrower one silently
      // clamps the gate and changes the model without changing the cost,
      // which is the bug found on 2026-09-13.
      inner.reset(new FssNormalizer<Ring>(s, p.t, 1, 2 * p.t + 8,
                                          2 * p.t + 8, kNewtonSteps));
    } catch (const std::exception& e) {
      std::fprintf(stderr, "  SKIP fss b=%d: %s\n", kBits, e.what());
      return false;
    }
  } else {
    inner.reset(new RevealNormNormalizer<Ring>());
  }
  CountingNormalizer<Ring> meter(inner.get());

  const auto t0 = std::chrono::steady_clock::now();
  auto res = ApproxFactorShared<Ring>(s, su, p, sched, meter, 0);
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();

  const StageModel mod = Model(m, n, d, ell, sched.Deferred(), use_fss,
                               kNewtonSteps);
  const std::uint64_t kB = RingTraits<Ring>::kBytes;
  const std::int64_t resid_r =
      static_cast<std::int64_t>(res.rounds) - static_cast<std::int64_t>(mod.Rounds());
  const std::int64_t resid_b =
      static_cast<std::int64_t>(res.bytes) -
      static_cast<std::int64_t>(mod.Elems() * kB);

  const char* norm_name = use_fss ? "fss" : "reveal";
  std::fprintf(stderr,
               "\n  b=%d ell=%u %s: %llu rounds, %.1f MB, %.1f s\n", kBits, ell,
               norm_name, (unsigned long long)res.rounds,
               static_cast<double>(res.bytes) / 1e6, ms / 1000.0);
  std::fprintf(stderr, "    %-12s %10s %8s %12s %8s\n", "phase", "rounds",
               "share", "bytes", "share");

  struct Named { const char* name; const Budget* b; };
  const Named phases[] = {{"matvec", &mod.matvec},
                          {"truncate", &mod.truncate},
                          {"normalize", &mod.normalize},
                          {"fss", &mod.fss},
                          {"net", &mod.open_b}};   // opening B: see header

  for (const auto& ph : phases) {
    const double sr = mod.Rounds() ? 100.0 * ph.b->rounds / mod.Rounds() : 0.0;
    const double sb = mod.Elems() ? 100.0 * ph.b->elems / mod.Elems() : 0.0;
    std::fprintf(stderr, "    %-12s %10llu %7.1f%% %12llu %7.1f%%\n", ph.name,
                 (unsigned long long)ph.b->rounds, sr,
                 (unsigned long long)(ph.b->elems * kB), sb);

    Row r;
    r.stage = "S2";
    r.phase = ph.name;
    r.m = m; r.n = n; r.d = d; r.ell = ell; r.b = kBits; r.t = 20;
    r.wall_ms = 0.0;                       // analytic: rounds/bytes only
    r.bytes_sent = static_cast<long long>(ph.b->elems * kB);
    r.ExtraStr("op", "attrib_analytic");
    r.Extra("rounds", static_cast<long long>(ph.b->rounds));
    r.Extra("share_rounds", sr);
    r.Extra("share_bytes", sb);
    r.ExtraStr("normalizer", norm_name);
    r.ExtraStr("deferred", sched.Deferred() ? "true" : "false");
    r.Extra("total_rounds", static_cast<long long>(res.rounds));
    r.Extra("total_bytes", static_cast<long long>(res.bytes));
    r.Extra("residual_rounds", static_cast<long long>(resid_r));
    r.Extra("residual_bytes", static_cast<long long>(resid_b));
    r.ExtraStr("model", "closed-form");
    w.Emit(r);
  }

  // ---- the SECOND, independent check ------------------------------------
  //
  // The analytic model and the in-situ meter measure overlapping things by
  // different means, so they must agree. Everything the meter sees is the
  // normaliser's own rounds plus the TruncatePair calls it makes internally,
  // and the model knows how many of those there are per call:
  //
  //   reveal: 1 truncation per call            ->  3 rounds
  //   fss:    1 + 1 + 3*newton per call        ->  3*(2 + 3*newton) rounds
  //
  // If this disagrees, either the model is wrong or the protocol changed. It
  // is worth having because the residual check alone would still pass if two
  // phases were mis-attributed BETWEEN each other.
  const std::uint64_t model_norm_total = mod.normalize.rounds + mod.fss.rounds;
  const std::uint64_t trunc_per_call =
      use_fss ? 3 * (2 + 3 * static_cast<std::uint64_t>(kNewtonSteps)) : 3;
  const std::uint64_t expect_meter =
      model_norm_total + meter.calls() * trunc_per_call;
  const bool meter_agrees = (expect_meter == meter.rounds());

  std::fprintf(stderr,
               "    in-situ normalizer: %llu calls, %llu rounds, %.1f ms "
               "(model predicts %llu = %llu own + %llu internal truncate) %s\n",
               (unsigned long long)meter.calls(),
               (unsigned long long)meter.rounds(), meter.ms(),
               (unsigned long long)expect_meter,
               (unsigned long long)model_norm_total,
               (unsigned long long)(meter.calls() * trunc_per_call),
               meter_agrees ? "AGREE" : "*** DISAGREE ***");
  if (!meter_agrees) ++(*failures);

  Row nr;
  nr.stage = "S2";
  nr.phase = "normalize";
  nr.m = m; nr.n = n; nr.d = d; nr.ell = ell; nr.b = kBits; nr.t = 20;
  nr.wall_ms = meter.calls() ? meter.ms() / static_cast<double>(meter.calls()) : 0.0;
  nr.bytes_sent = static_cast<long long>(meter.bytes());
  nr.ExtraStr("op", "attrib_insitu");
  nr.Extra("calls", static_cast<long long>(meter.calls()));
  nr.Extra("rounds", static_cast<long long>(meter.rounds()));
  nr.Extra("rounds_per_call",
           meter.calls() ? static_cast<double>(meter.rounds()) / meter.calls() : 0.0);
  nr.Extra("ms_total", meter.ms());
  nr.ExtraStr("normalizer", norm_name);
  nr.Extra("total_rounds", static_cast<long long>(res.rounds));
  w.Emit(nr);

  if (resid_r != 0 || resid_b != 0) {
    std::fprintf(stderr,
                 "    MODEL DISAGREES: residual %lld rounds, %lld bytes.\n"
                 "    The closed form no longer describes the protocol. Either\n"
                 "    a charge changed or a stage was added; do not publish a\n"
                 "    breakdown that does not add up.\n",
                 (long long)resid_r, (long long)resid_b);
    ++(*failures);
  } else {
    std::fprintf(stderr,
                 "    residual 0 rounds, 0 bytes -- the breakdown accounts for "
                 "EVERY round and byte.\n");
  }

  // The network view of the same run, from the same counters.
  Row proto;
  proto.stage = "S2";
  proto.m = m; proto.n = n; proto.d = d; proto.ell = ell;
  proto.b = kBits; proto.t = 20;
  EmitProfiles(w, proto, res.rounds, res.bytes, "normalizer", norm_name,
               "attrib_total");
  return true;
}

// ------------------------------------------------------------------------
//  (B) standalone per-protocol wall time. A RATE, out of context; see header.
// ------------------------------------------------------------------------
void MicroTime(Writer& w) {
  const std::uint32_t n = kFullN, m = kFullM;
  std::fprintf(stderr, "\n  per-protocol wall time (standalone, out of the "
                       "training loop's cache context)\n");

  auto base = [&](const char* phase, const char* op) {
    Row r;
    r.stage = "S2";
    r.phase = phase;
    r.n = n; r.b = 64; r.t = 20;
    r.ExtraStr("op", op);
    return r;
  };

  // matvec, both shapes the loop uses.
  {
    Mpc3<u64> s(1);
    std::vector<u64> Uv(std::size_t(m) * n, u64(3));
    SharedMatrix<u64> U;
    U.rows = m; U.cols = n;
    U.data = SplitVec<u64>(Span<const u64>(Uv.data(), Uv.size()));
    std::vector<u64> vv(n, u64(1) << 20);
    auto v = SplitVec<u64>(Span<const u64>(vv.data(), vv.size()));
    Row r = base("matvec", "matvec_shared_mn");
    r.m = m;
    const std::uint64_t r0 = s.Rounds(), b0 = s.BytesSent();
    auto out = s.MatVec(U, v);
    r.Extra("rounds", static_cast<long long>(s.Rounds() - r0));
    r.bytes_sent = static_cast<long long>(s.BytesSent() - b0);
    TimeAndEmit(w, r, kReps, [&] { auto o = s.MatVec(U, v); Sink(o.p[0][0]); });
  }

  // truncation, at both lengths the protocol actually uses.
  for (std::uint32_t len : {std::uint32_t(1), kFullN}) {
    Mpc3<u64> s(2);
    std::vector<u64> xv(len, u64(1) << 33);
    auto x = SplitVec<u64>(Span<const u64>(xv.data(), xv.size()));
    Row r = base("truncate", len == 1 ? "truncate_pair_1" : "truncate_pair_n");
    r.n = static_cast<long long>(len);
    const std::uint64_t r0 = s.Rounds(), b0 = s.BytesSent();
    auto o0 = TruncatePair<u64>(s, x, 20);
    Sink(o0.p[0][0]);
    r.Extra("rounds", static_cast<long long>(s.Rounds() - r0));
    r.bytes_sent = static_cast<long long>(s.BytesSent() - b0);
    TimeAndEmit(w, r, kReps,
                [&] { auto o = TruncatePair<u64>(s, x, 20); Sink(o.p[0][0]); });
  }

  // the revealing normaliser, in isolation.
  {
    Mpc3<u64> s(3);
    std::vector<u64> vv(n, u64(1) << 18);
    auto v = SplitVec<u64>(Span<const u64>(vv.data(), vv.size()));
    RevealNormNormalizer<u64> rn;
    Row r = base("normalize", "normalize_reveal");
    const std::uint64_t r0 = s.Rounds(), b0 = s.BytesSent();
    auto o0 = rn.Apply(s, v, 20);
    Sink(o0.p[0][0]);
    r.Extra("rounds", static_cast<long long>(s.Rounds() - r0));
    r.bytes_sent = static_cast<long long>(s.BytesSent() - b0);
    TimeAndEmit(w, r, kReps, [&] { auto o = rn.Apply(s, v, 20); Sink(o.p[0][0]); });
  }

  // the no-leak normaliser and its gate need the wide ring.
  {
    Mpc3<u128> s(4);
    std::vector<u128> vv(n, u128(1) << 18);
    auto v = SplitVec<u128>(Span<const u128>(vv.data(), vv.size()));
    try {
      FssNormalizer<u128> fn(s, 20, 1, 48, 48, kNewtonSteps);
      Row r = base("normalize", "normalize_fss");
      r.b = 128;
      const std::uint64_t r0 = s.Rounds(), b0 = s.BytesSent();
      auto o0 = fn.Apply(s, v, 20);
      Sink(o0.p[0][0]);
      r.Extra("rounds", static_cast<long long>(s.Rounds() - r0));
      r.bytes_sent = static_cast<long long>(s.BytesSent() - b0);
      TimeAndEmit(w, r, kReps, [&] { auto o = fn.Apply(s, v, 20); Sink(o.p[0][0]); });

      MsnzbGate<u128> gate(s, 1, 48, 48);
      std::vector<u128> table(48, u128(1) << 20);
      std::vector<u128> sv(1, u128(1) << 24);
      auto S = SplitVec<u128>(Span<const u128>(sv.data(), sv.size()));
      Row g = base("fss", "msnzb_gate");
      g.b = 128; g.n = 1;
      g.Extra("key_bytes", static_cast<long long>(gate.KeyBytes()));
      g.Extra("domain_bits", static_cast<long long>(gate.DomainBits()));
      const std::uint64_t gr0 = s.Rounds(), gb0 = s.BytesSent();
      auto go = gate.Apply(s, S, table);
      Sink(go.p[0][0]);
      g.Extra("rounds", static_cast<long long>(s.Rounds() - gr0));
      g.bytes_sent = static_cast<long long>(s.BytesSent() - gb0);
      TimeAndEmit(w, g, kReps,
                  [&] { auto o = gate.Apply(s, S, table); Sink(o.p[0][0]); });
    } catch (const std::exception& e) {
      std::fprintf(stderr, "  SKIP fss micro: %s\n", e.what());
    }
  }

  // scoring and selection -- the first phase="topk" rows this project emits.
  {
    Catalogue cat = Catalogue::LoadMovieLens("data/ml-100k/u.item");
    const std::uint32_t items = cat.NumItems(), d = 16;
    std::vector<std::int64_t> B(std::size_t(d) * items, 1 << 18);
    std::vector<ReplicatedShare<u64>> a_share(d);
    for (std::uint32_t i = 0; i < d; ++i) {
      a_share[i] = ReplicatedShare<u64>(u64(1) << 18, u64(0));
    }
    auto mask = BuildMaskShares<u64>(std::vector<std::uint8_t>(items, 0), items);
    std::vector<ReplicatedShare<u64>> out(items);
    std::vector<u64> Bring(B.begin(), B.end());

    Row r = base("topk", "score_shares");
    r.n = items; r.d = d;
    r.bytes_sent = 0;                      // public matrix on shares: 0 rounds
    r.Extra("rounds", 0LL);
    TimeAndEmit(w, r, kReps, [&] {
      ScoreShares<u64>(a_share, Span<const u64>(Bring.data(), Bring.size()), d,
                       items, mask[0], out);
      Sink(out[0]);
    });

    std::vector<std::int64_t> scores(items);
    for (std::uint32_t j = 0; j < items; ++j) scores[j] = std::int64_t(j) * 7;
    Row t = base("topk", "topk_select");
    t.n = items; t.k = 10;
    t.bytes_sent = 0;
    t.Extra("rounds", 0LL);
    TimeAndEmit(w, t, kReps, [&] {
      auto top = TopK(Span<const std::int64_t>(scores.data(), scores.size()), 10);
      Sink(top[0]);
    });

    // the private read, for the delivery half of the breakdown.
    PirServer<u64> p0(cat);
    PirClient<u64> client(cat.DomainBits());
    auto keys = client.Query(7);
    std::vector<u64> ans(RecordWords<u64>());
    Row pr = base("pir", "pir_answer");
    pr.n = items;
    pr.bytes_sent = static_cast<long long>(keys.first.SizeBytes() +
                                           RecordWords<u64>() * sizeof(u64));
    pr.Extra("rounds", 1LL);
    pr.Extra("domain_bits", static_cast<long long>(cat.DomainBits()));
    TimeAndEmit(w, pr, kReps, [&] {
      p0.Answer(keys.first, Span<u64>(ans.data(), ans.size()));
      Sink(ans[0]);
    });
  }
}

}  // namespace

int main() {
  Writer w(stdout);
  Ratings rt;
  if (!LoadRatings("data/ml-100k/u1.base", &rt)) {
    std::fprintf(stderr,
                 "bench_stages: data/ml-100k/u1.base missing. Run "
                 "py -3.13 scripts/fetch_data.py\n");
    return 0;                       // emit nothing rather than invent rows
  }

  int failures = 0;
  std::fprintf(stderr, "task 4.3 -- where the rounds and the bytes go\n");
  Attribute<u64>(w, rt, 16, 10, false, &failures);
  Attribute<u128>(w, rt, 16, 10, false, &failures);
  Attribute<u128>(w, rt, 16, 10, true, &failures);

  MicroTime(w);

  if (failures) {
    std::fprintf(stderr,
                 "\nbench_stages: %d configuration(s) did not reconcile.\n",
                 failures);
    return 1;
  }
  std::fprintf(stderr, "\nbench_stages: every configuration reconciled exactly.\n");
  return 0;
}
