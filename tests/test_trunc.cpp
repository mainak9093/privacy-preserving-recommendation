// ==========================================================================
//  test_trunc.cpp -- Trunc_t, measured rather than merely exercised.
//
//  ARCHITECTURE section 4: "both protocols are tested against a cleartext
//  fixed-point oracle over randomised inputs, with the observed error bound
//  recorded. An approximate protocol whose error is not measured is not
//  finished."
//
//  So this file does not just assert "passes". It reports the error
//  DISTRIBUTION for each variant, and the local variant's catastrophic-failure
//  rate, because that number is the entire argument for paying three rounds.
// ==========================================================================
#include "oblivrec/nonlinear.hpp"
#include "oblivrec_test.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

constexpr std::uint32_t kT = 20;

// Signed distance between a protocol result and the oracle, as a plain
// integer, so "off by one" reads as 1 rather than as 2^64 - 1.
template <typename Ring>
long long SignedDiff(Ring got, Ring want) {
  using S = typename RingTraits<Ring>::Signed;
  return static_cast<long long>(static_cast<S>(static_cast<Ring>(got - want)));
}

// ---- the oracle itself must be right ------------------------------------
void TestOracle() {
  CHECK(TruncateClear<u64>(u64(1) << 20, kT) == u64(1));
  CHECK(TruncateClear<u64>(u64(3) << 20, kT) == u64(3));
  // Negative values must stay negative: an arithmetic shift, not a logical one.
  const u64 neg = static_cast<u64>(-(std::int64_t(5) << 20));
  CHECK_MSG(static_cast<std::int64_t>(TruncateClear<u64>(neg, kT)) == -5,
            "TruncateClear turned a negative value positive -- that is a "
            "logical shift where an arithmetic one is required");
  CHECK(static_cast<std::int64_t>(TruncateClear<u64>(
            static_cast<u64>(std::int64_t(-1)), kT)) == -1);
  std::printf("  oracle: arithmetic shift, sign preserved\n");
}

// ---- the exact protocol -------------------------------------------------
void TestPairExact() {
  std::mt19937_64 rng(20260911);
  Mpc3<u64> s(5);

  long long worst = 0;
  std::vector<long long> hist(4, 0);   // |error| of 0, 1, 2, or more
  const int trials = 400;
  const std::size_t n = 8;

  for (int t = 0; t < trials; ++t) {
    // Values in a realistic fixed-point range: |v| < 2^40, so the 2t-scaled
    // products this protocol actually sees are represented.
    std::vector<u64> x(n);
    for (auto& v : x) {
      const std::int64_t mag = static_cast<std::int64_t>(rng() % (1ull << 40));
      v = static_cast<u64>((rng() & 1) ? mag : -mag);
    }
    auto sx = SplitVec<u64>(Span<const u64>(x.data(), n));
    auto got = OpenVec<u64>(TruncatePair<u64>(s, sx, kT));

    for (std::size_t j = 0; j < n; ++j) {
      const long long d = SignedDiff<u64>(got[j], TruncateClear<u64>(x[j], kT));
      const long long ad = d < 0 ? -d : d;
      worst = std::max(worst, ad);
      hist[static_cast<std::size_t>(std::min<long long>(ad, 3))]++;
    }
  }

  const long long total = trials * static_cast<long long>(n);
  // The construction admits a one-unit borrow. Anything larger means the
  // protocol is wrong, not approximate.
  CHECK_MSG(worst <= 1,
            "TruncatePair worst error was " + std::to_string(worst) +
                " units; the construction admits at most 1 (the borrow)");
  std::printf("  TruncatePair: %lld values, worst |error| = %lld unit(s)\n",
              total, worst);
  std::printf("               error 0: %.1f%%   error 1: %.1f%%\n",
              100.0 * static_cast<double>(hist[0]) / static_cast<double>(total),
              100.0 * static_cast<double>(hist[1]) / static_cast<double>(total));
}

// ---- the naive variant, and what it actually costs ----------------------
void TestLocalFailureRate() {
  std::mt19937_64 rng(777);
  const int trials = 2000;
  const std::size_t n = 4;

  long long catastrophic = 0, small = 0, exact = 0;
  long long worst_small = 0;

  for (int t = 0; t < trials; ++t) {
    std::vector<u64> x(n);
    for (auto& v : x) {
      const std::int64_t mag = static_cast<std::int64_t>(rng() % (1ull << 40));
      v = static_cast<u64>((rng() & 1) ? mag : -mag);
    }
    auto sx = SplitVec<u64>(Span<const u64>(x.data(), n));
    auto got = OpenVec<u64>(TruncateLocal<u64>(sx, kT));

    for (std::size_t j = 0; j < n; ++j) {
      const long long d = SignedDiff<u64>(got[j], TruncateClear<u64>(x[j], kT));
      const long long ad = d < 0 ? -d : d;
      if (ad == 0) {
        ++exact;
      } else if (ad <= 4) {
        ++small;
        worst_small = std::max(worst_small, ad);
      } else {
        ++catastrophic;   // a wrap contributes ~2^(b-t); nothing in between
      }
    }
  }

  const long long total = trials * static_cast<long long>(n);
  const double fail_pct =
      100.0 * static_cast<double>(catastrophic) / static_cast<double>(total);

  std::printf("  TruncateLocal: %lld values -> exact %.1f%%, small error "
              "(<=%lld) %.1f%%, CATASTROPHIC %.1f%%\n",
              total, 100.0 * static_cast<double>(exact) / static_cast<double>(total),
              worst_small,
              100.0 * static_cast<double>(small) / static_cast<double>(total),
              fail_pct);

  // This is the finding, not a failure of the test. Local truncation on
  // uniformly random replicated shares wraps often, and a wrap is not a
  // rounding error -- it is a wrong answer of order 2^(b-t). If this ever came
  // out near zero, the sharing would not be uniform and THAT would be the bug.
  CHECK_MSG(catastrophic > 0,
            "TruncateLocal produced no wraparound failures at all, which "
            "contradicts uniform shares -- suspect the sharing, not the shift");
  std::printf("               ^ this is why Trunc_t costs 3 rounds rather "
              "than 0\n");
}

// ---- rounds are charged, and batching is free --------------------------
void TestRoundAccounting() {
  Mpc3<u64> s(9);
  std::vector<u64> one(1, 12345), many(64, 12345);

  s.ResetCounters();
  (void)TruncatePair<u64>(s, SplitVec<u64>(Span<const u64>(one.data(), 1)), kT);
  const std::uint64_t r1 = s.Rounds(), b1 = s.BytesSent();

  s.ResetCounters();
  (void)TruncatePair<u64>(s, SplitVec<u64>(Span<const u64>(many.data(), 64)), kT);
  const std::uint64_t r64 = s.Rounds(), b64 = s.BytesSent();

  CHECK_MSG(r1 == 3, "TruncatePair took " + std::to_string(r1) +
                         " rounds, expected 3 (ARCHITECTURE 4.1)");
  CHECK_MSG(r64 == r1,
            "truncating 64 values took " + std::to_string(r64) +
                " rounds but one value took " + std::to_string(r1) +
                " -- a batch must ride in the same messages");
  CHECK_MSG(b64 == b1 * 64, "bytes should scale linearly with the batch");
  std::printf("  accounting: 1 value -> %llu rounds/%llu B; "
              "64 values -> %llu rounds/%llu B (rounds flat, bytes linear)\n",
              (unsigned long long)r1, (unsigned long long)b1,
              (unsigned long long)r64, (unsigned long long)b64);

  // And the local variant really is free.
  s.ResetCounters();
  (void)TruncateLocal<u64>(SplitVec<u64>(Span<const u64>(many.data(), 64)), kT);
  CHECK_MSG(s.Rounds() == 0 && s.BytesSent() == 0,
            "TruncateLocal must cost nothing; that is its only virtue");
}

// ---- the helper must not be one of the openers -------------------------
void TestHelperRoles() {
  Mpc3<u64> s(13);
  std::vector<u64> x(4);
  for (std::size_t i = 0; i < x.size(); ++i) {
    // MULTIPLY, do not shift. `(-2) << 25` is a left shift of a negative
    // value, which is undefined behaviour in C++17 -- UBSan trapped on it and
    // that is how this line was found. Multiplication by a power of two is
    // well defined and generates the same instruction.
    x[i] = static_cast<u64>((std::int64_t(i) - 2) * (std::int64_t(1) << 25));
  }
  // Every choice of helper must give the same answer; the role is a rotation,
  // not a special case.
  for (int h = 0; h < 3; ++h) {
    auto sx = SplitVec<u64>(Span<const u64>(x.data(), x.size()));
    auto got = OpenVec<u64>(TruncatePair<u64>(s, sx, kT, h));
    for (std::size_t j = 0; j < x.size(); ++j) {
      const long long d = SignedDiff<u64>(got[j], TruncateClear<u64>(x[j], kT));
      CHECK_MSG(d >= -1 && d <= 1,
                "helper=" + std::to_string(h) + " gave error " +
                    std::to_string(d));
    }
  }
  bool threw = false;
  try { (void)TruncatePair<u64>(s, SplitVec<u64>(Span<const u64>(x.data(), 1)), kT, 7); }
  catch (const std::invalid_argument&) { threw = true; }
  CHECK_MSG(threw, "an out-of-range helper index was accepted");
  std::printf("  helper role: all three rotations agree; bad index rejected\n");
}

}  // namespace

int main() {
  std::printf("test_trunc\n");
  try {
    TestOracle();
    TestPairExact();
    TestLocalFailureRate();
    TestRoundAccounting();
    TestHelperRoles();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_trunc");
}
