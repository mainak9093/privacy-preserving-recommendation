// ==========================================================================
//  test_msnzb.cpp -- the FSS comparison gate and the shared inverse sqrt.
//
//  Two things worth asserting, and the second is the interesting one.
//
//  CORRECTNESS: the gate returns shares of a step function of msnzb(S), and
//  InvSqrtShared matches the cleartext reference that was already calibrated.
//
//  THE RING-WIDTH REFUSAL: a statistically hiding gate needs
//  L + kappa + 1 bits for the mask. At the value ranges power iteration
//  produces that exceeds 64, so the gate must REFUSE to build at b=64 rather
//  than quietly using a mask too small to hide anything. A gate that silently
//  degraded here would be worse than no gate: it would look like privacy.
// ==========================================================================
#include "oblivrec/msnzb.hpp"
#include "oblivrec/nonlinear.hpp"
#include "oblivrec_test.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

constexpr std::uint32_t kT = 20;

// ---- THE RING-WIDTH RESULT ---------------------------------------------
void TestRingWidthRefusal() {
  Mpc3<u64> s64(1);
  Mpc3<u128> s128(1);

  // ||v||^2 at t=20 occupies roughly 40 bits. Plus kappa=40 plus a carry bit
  // is 81 -- more than a 64-bit ring can hold.
  const std::uint32_t value_bits = 40;

  bool refused = false;
  try {
    MsnzbGate<u64> gate(s64, 10, 30, value_bits);
    (void)gate;
  } catch (const std::exception&) {
    refused = true;
  }
  CHECK_MSG(refused,
            "b=64 accepted a gate needing " + std::to_string(value_bits + kMaskKappa + 1) +
                " bits of mask domain; it must refuse rather than use a mask "
                "too small to hide the value");

  // The same gate at b=128 must build.
  bool built = false;
  try {
    MsnzbGate<u128> gate(s128, 10, 30, value_bits);
    built = gate.DomainBits() == value_bits + kMaskKappa + 1;
    std::printf("  b=128: gate builds, domain %u bits (%u value + %u kappa "
                "+ 1), %zu KB of keys\n", gate.DomainBits(), value_bits,
                kMaskKappa, gate.KeyBytes() / 1024);
  } catch (const std::exception& e) {
    std::printf("  b=128 unexpectedly refused: %s\n", e.what());
  }
  CHECK_MSG(built, "b=128 should support the gate at these ranges");
  std::printf("  D9.1, sharpened: a spec-faithful normaliser needs b=128. "
              "b=64 has no room for the mask.\n");
}

// ---- the gate computes a step function of msnzb -------------------------
void TestGateStepFunction() {
  // A narrow value range so the gate is cheap: msnzb in [4, 12], 16-bit values.
  const std::uint32_t lo = 4, hi = 12, value_bits = 16;
  Mpc3<u128> s(7);
  MsnzbGate<u128> gate(s, lo, hi, value_bits);

  // table[k-lo] = k, so the gate should return msnzb(S) itself.
  std::vector<u128> table;
  for (std::uint32_t k = lo; k <= hi; ++k) table.push_back(static_cast<u128>(k));

  std::mt19937_64 rng(4242);
  std::vector<u128> vals;
  for (std::uint32_t k = lo; k <= hi; ++k) {
    // A value whose msnzb is exactly k.
    const std::uint64_t base = std::uint64_t(1) << k;
    const std::uint64_t extra = (k == 0) ? 0 : (rng() % base);
    vals.push_back(static_cast<u128>(base + extra));
  }

  auto shared = SplitVec<u128>(Span<const u128>(vals.data(), vals.size()));
  auto got = OpenVec<u128>(gate.Apply(s, shared, table));

  int wrong = 0;
  for (std::size_t i = 0; i < vals.size(); ++i) {
    const std::uint32_t want = lo + static_cast<std::uint32_t>(i);
    const std::uint64_t g = static_cast<std::uint64_t>(got[i]);
    if (g != want) {
      ++wrong;
      if (wrong <= 3) {
        std::printf("    msnzb mismatch: value 2^%u.. -> got %llu, want %u\n",
                    want, (unsigned long long)g, want);
      }
    }
  }
  CHECK_MSG(wrong == 0,
            std::to_string(wrong) + " of " + std::to_string(vals.size()) +
                " msnzb values are wrong");
  std::printf("  gate: msnzb correct for every k in [%u, %u], one round\n",
              lo, hi);
}

// ---- the shared inverse sqrt matches the cleartext reference -----------
void TestInvSqrtShared() {
  const std::uint32_t lo = 12, hi = 28, value_bits = 30;
  Mpc3<u128> s(11);
  MsnzbGate<u128> gate(s, lo, hi, value_bits);

  std::mt19937_64 rng(99);
  std::vector<u128> vals;
  std::vector<double> truth;
  for (int i = 0; i < 8; ++i) {
    // ||v||^2 values around 1, encoded at scale t.
    const double sv = 0.25 + static_cast<double>(rng() % 4000) / 1000.0;
    vals.push_back(static_cast<u128>(
        static_cast<std::uint64_t>(sv * std::pow(2.0, kT))));
    truth.push_back(1.0 / std::sqrt(sv));
  }

  auto shared = SplitVec<u128>(Span<const u128>(vals.data(), vals.size()));
  s.ResetCounters();
  auto y = InvSqrtShared<u128>(s, shared, kT, 4, gate);
  const std::uint64_t rounds = s.Rounds();
  auto got = OpenVec<u128>(y);

  double worst = 0.0;
  for (std::size_t i = 0; i < vals.size(); ++i) {
    const double g = static_cast<double>(static_cast<std::int64_t>(
                         static_cast<std::uint64_t>(got[i]))) /
                     std::pow(2.0, kT);
    worst = std::max(worst, std::abs(g - truth[i]) / truth[i]);
  }
  std::printf("  InvSqrtShared: 8 values, 4 Newton steps, worst relative "
              "error %.2e, %llu rounds\n", worst, (unsigned long long)rounds);

  // The cleartext reference reached 4.7e-4 at four steps; the shared version
  // adds one truncation-unit of error per call, so a slightly looser bound.
  CHECK_MSG(worst < 5e-3,
            "shared inverse sqrt worst relative error " +
                std::to_string(worst) +
                " exceeds 5e-3; the cleartext reference reaches 4.7e-4 at the "
                "same step count, so this is a protocol problem not a "
                "numerical one");
}

}  // namespace

int main() {
  std::printf("test_msnzb\n");
  try {
    TestRingWidthRefusal();
    TestGateStepFunction();
    TestInvSqrtShared();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_msnzb");
}
