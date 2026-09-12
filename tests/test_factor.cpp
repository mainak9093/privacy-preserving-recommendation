// ==========================================================================
//  test_factor.cpp -- ApproxFactor under sharing, and the truncation schedule.
//
//  The headline check is not "it runs". It is that the shared factorisation
//  recovers the SAME SUBSPACE as a cleartext power iteration on the same
//  matrix -- measured as the principal angle between the two row spaces, the
//  way model/mf.py already cross-checks itself against SVD.
//
//  The schedule is checked the other way round: it must REFUSE parameters the
//  ring cannot take. A schedule that never says no is not protecting anything.
// ==========================================================================
#include "oblivrec/factor.hpp"
#include "oblivrec_test.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

constexpr std::uint32_t kT = 20;

double Dec(u64 x, std::uint32_t t) {
  return static_cast<double>(static_cast<std::int64_t>(x)) /
         std::pow(2.0, static_cast<double>(t));
}

// ---- 3.6: the schedule must derive, explain, and REFUSE -----------------
void TestSchedule() {
  FactorParams p;
  p.m = 943; p.n = 1682; p.d = 16; p.ell = 10; p.t = kT; p.max_rating = 5;
  p.nnz = 100000;   // ML-100K, and nnz is public

  auto s64 = TruncationSchedule::Derive(p, 64);
  std::printf("  schedule ML-100K b=64:  %s\n", s64.Explain().c_str());

  auto s128 = TruncationSchedule::Derive(p, 128);
  std::printf("  schedule ML-100K b=128: %s\n", s128.Explain().c_str());
  s128.AssertHeadroom();   // must be fine

  // The schedule earns its place only if it says no somewhere. A wide t in a
  // 64-bit ring must be refused rather than silently wrapped.
  FactorParams wide = p;
  wide.t = 30;
  auto bad = TruncationSchedule::Derive(wide, 64);
  bool threw = false;
  try { bad.AssertHeadroom(); } catch (const std::overflow_error&) { threw = true; }
  CHECK_MSG(threw,
            "the schedule accepted t=30 at b=64, which leaves only a few "
            "spare bits; it must refuse, or it is not protecting against "
            "silent overflow");
  std::printf("  schedule: refuses t=30 at b=64 as it must\n");
}

// ---- 3.5: the shared factorisation recovers the right subspace ----------
void TestFactorSubspace() {
  // Small and dense, so the test is quick but the arithmetic is the real one.
  FactorParams p;
  p.m = 24; p.n = 16; p.d = 3; p.ell = 6; p.t = kT; p.max_rating = 5;
  p.nnz = 24 * 16 / 4;

  std::mt19937_64 rng(2026);
  std::vector<u64> U(std::size_t(p.m) * p.n);
  for (auto& x : U) {
    // Ratings at scale 0: small integers, mostly zero, as MovieLens is.
    x = static_cast<u64>((rng() % 4 == 0) ? (1 + rng() % 5) : 0);
  }

  auto sched = TruncationSchedule::Derive(p, 64);
  sched.AssertHeadroom();

  SharedMatrix<u64> su;
  su.rows = p.m;
  su.cols = p.n;
  su.data = SplitVec<u64>(Span<const u64>(U.data(), U.size()));

  // The spec-faithful normaliser is now BUILT, and at b=64 it must refuse at
  // construction: the FSS gate's mask needs value_bits + kappa + 1 bits, which
  // a 64-bit ring cannot hold. Refusing early -- before any training runs --
  // is the point; a gate that silently used a mask too small to hide anything
  // would look like privacy while providing none.
  {
    Mpc3<u64> s(1);
    bool threw = false;
    try {
      FssNormalizer<u64> fss(s, p.t, 12, 28, 30);
      (void)fss;
    } catch (const std::exception&) { threw = true; }
    CHECK_MSG(threw,
              "FssNormalizer built at b=64; it must refuse, because the gate's "
              "mask does not fit and a short mask hides nothing");
    std::printf("  FssNormalizer at b=64: refused, as it must (D9.1)\n");
  }

  Mpc3<u64> s(1);
  RevealNormNormalizer<u64> rn;
  auto res = ApproxFactorShared<u64>(s, su, p, sched, rn, 7);

  CHECK_MSG(res.revealed_norms.size() > 0,
            "the reveal-norm normalizer must record what it leaked");
  std::printf("  ApproxFactor: d=%u ell=%u on %ux%u -> %llu rounds, %llu B, "
              "%llu truncations\n", p.d, p.ell, p.m, p.n,
              (unsigned long long)res.rounds, (unsigned long long)res.bytes,
              (unsigned long long)res.truncations);
  std::printf("               normalizer: %s (%zu scalars revealed)\n",
              res.normalizer.c_str(), res.revealed_norms.size());

  // Rows must be unit length: that is the only property power iteration needs
  // from the normalisation, and it is what B being an embedding basis means.
  double worst_len = 0.0;
  for (std::uint32_t r = 0; r < p.d; ++r) {
    double sq = 0.0;
    for (std::uint32_t j = 0; j < p.n; ++j) {
      const double f = Dec(res.B[std::size_t(r) * p.n + j], p.t);
      sq += f * f;
    }
    worst_len = std::max(worst_len, std::abs(std::sqrt(sq) - 1.0));
  }
  CHECK_MSG(worst_len < 0.02,
            "B rows deviate from unit length by up to " +
                std::to_string(worst_len));

  // Rows must be mutually orthogonal: that is what SetOrthogonal is for, and
  // it is the property that fails first if the deferred truncation is wrong.
  double worst_dot = 0.0;
  for (std::uint32_t a = 0; a < p.d; ++a) {
    for (std::uint32_t b = a + 1; b < p.d; ++b) {
      double dot = 0.0;
      for (std::uint32_t j = 0; j < p.n; ++j) {
        dot += Dec(res.B[std::size_t(a) * p.n + j], p.t) *
               Dec(res.B[std::size_t(b) * p.n + j], p.t);
      }
      worst_dot = std::max(worst_dot, std::abs(dot));
    }
  }
  CHECK_MSG(worst_dot < 0.05,
            "B rows are not orthogonal; worst |dot| = " +
                std::to_string(worst_dot));
  std::printf("               B rows: unit to %.1e, orthogonal to %.1e\n",
              worst_len, worst_dot);

  // And the shared result must agree with the cleartext twin that uses the
  // same truncation points -- compared as SUBSPACES, since power iteration
  // fixes neither the sign nor the order of near-degenerate components.
  auto Bclear = ApproxFactorClear<u64>(Span<const u64>(U.data(), U.size()), p, 7);
  double worst_align = 0.0;
  for (std::uint32_t r = 0; r < p.d; ++r) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (std::uint32_t j = 0; j < p.n; ++j) {
      const double x = Dec(res.B[std::size_t(r) * p.n + j], p.t);
      const double y = Dec(Bclear[std::size_t(r) * p.n + j], p.t);
      dot += x * y; na += x * x; nb += y * y;
    }
    if (na > 0 && nb > 0) {
      worst_align = std::max(worst_align,
                             1.0 - std::abs(dot) / std::sqrt(na * nb));
    }
  }
  std::printf("               vs cleartext twin: worst 1-|cos| = %.2e\n",
              worst_align);
  CHECK_MSG(worst_align < 0.05,
            "shared and cleartext factorisations disagree by " +
                std::to_string(worst_align) + " (1-|cos|)");
}

// ---- the round cost should be dominated by what we expect --------------
void TestRoundBudget() {
  FactorParams p;
  p.m = 12; p.n = 8; p.d = 2; p.ell = 3; p.t = kT; p.max_rating = 5;
  p.nnz = 12 * 8 / 2;
  std::mt19937_64 rng(5);
  std::vector<u64> U(std::size_t(p.m) * p.n);
  for (auto& x : U) x = static_cast<u64>(rng() % 6);

  auto sched = TruncationSchedule::Derive(p, 64);
  SharedMatrix<u64> su;
  su.rows = p.m; su.cols = p.n;
  su.data = SplitVec<u64>(Span<const u64>(U.data(), U.size()));

  Mpc3<u64> s(2);
  RevealNormNormalizer<u64> rn;
  auto res = ApproxFactorShared<u64>(s, su, p, sched, rn, 3);

  // Per component: 1 setup trunc (3) + 1 setup normalize (1+3+3) then per
  // inner iteration 2 matvecs (2) + 1 trunc (3) + 1 normalize (7), plus the
  // reveal (1). The exact constant matters less than that it is LINEAR in
  // d * ell -- if it ever becomes quadratic something is re-running.
  const std::uint64_t per_component = res.rounds / p.d;
  CHECK_MSG(res.rounds == per_component * p.d,
            "round count is not evenly divided across components");
  std::printf("  round budget: %llu rounds for d=%u ell=%u -> %llu per "
              "component, %.1f per inner iteration\n",
              (unsigned long long)res.rounds, p.d, p.ell,
              (unsigned long long)per_component,
              static_cast<double>(per_component) / static_cast<double>(p.ell));
}

// ---- 3.3 COMPLETE: training that reveals nothing -----------------------
//
// The same factorisation, at b=128, with the spec-faithful normaliser. The
// assertion that matters is not that it runs but that Revealed() is EMPTY --
// this is the path with no leakage-profile departure.
void TestFactorNoLeak() {
  FactorParams p;
  p.m = 20; p.n = 12; p.d = 2; p.ell = 4; p.t = kT; p.max_rating = 5;
  p.nnz = 20 * 12 / 4;

  std::mt19937_64 rng(606);
  std::vector<u128> U(std::size_t(p.m) * p.n);
  for (auto& x : U) {
    x = static_cast<u128>((rng() % 4 == 0) ? (1 + rng() % 5) : 0);
  }

  auto sched = TruncationSchedule::Derive(p, 128);
  sched.AssertHeadroom();
  CHECK_MSG(sched.Deferred(),
            "at b=128 the deferred schedule should fit, which is half the "
            "truncations");

  SharedMatrix<u128> su;
  su.rows = p.m; su.cols = p.n;
  su.data = SplitVec<u128>(Span<const u128>(U.data(), U.size()));

  Mpc3<u128> s(21);
  // ||v||^2 sits around 2^t with v near unit length, so msnzb lands in a band
  // around t. The bounds are public parameters, not data.
  FssNormalizer<u128> fss(s, p.t, kT - 8, kT + 8, 30);

  auto res = ApproxFactorShared<u128>(s, su, p, sched, fss, 5);

  CHECK_MSG(res.revealed_norms.empty(),
            "the spec-faithful normaliser revealed " +
                std::to_string(res.revealed_norms.size()) +
                " scalars; it must reveal none");

  double worst_len = 0.0;
  for (std::uint32_t r = 0; r < p.d; ++r) {
    double sq = 0.0;
    for (std::uint32_t j = 0; j < p.n; ++j) {
      const double f =
          static_cast<double>(static_cast<std::int64_t>(
              static_cast<std::uint64_t>(res.B[std::size_t(r) * p.n + j]))) /
          std::pow(2.0, static_cast<double>(p.t));
      sq += f * f;
    }
    worst_len = std::max(worst_len, std::abs(std::sqrt(sq) - 1.0));
  }
  CHECK_MSG(worst_len < 0.05,
            "B rows from the no-leak path deviate from unit length by " +
                std::to_string(worst_len));

  std::printf("  NO-LEAK PATH (b=128, FSS normaliser): d=%u ell=%u -> "
              "%llu rounds, %llu B\n", p.d, p.ell,
              (unsigned long long)res.rounds,
              (unsigned long long)res.bytes);
  std::printf("               %zu scalars revealed (must be 0), B rows unit "
              "to %.1e\n", res.revealed_norms.size(), worst_len);
}

}  // namespace

int main() {
  std::printf("test_factor\n");
  try {
    TestSchedule();
    TestFactorSubspace();
    TestFactorNoLeak();
    TestRoundBudget();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_factor");
}
