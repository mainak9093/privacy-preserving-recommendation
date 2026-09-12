// ==========================================================================
//  test_normalize.cpp -- the ApproxNormalize cleartext reference.
//
//  This measures the thing the risk register says will block Phase 3: how many
//  Newton-Raphson steps inverse square root actually needs in fixed point, and
//  what error remains. The answer is a TUNABLE, and the point of writing the
//  oracle before the protocol is to fix that tunable with data rather than
//  discover it while debugging secret sharing.
//
//  It also prints the round count the shared protocol will cost at the chosen
//  step count, derived from the reference's own structure. That is a budget
//  for work not yet done, which is more useful than an estimate.
// ==========================================================================
#include "oblivrec/nonlinear.hpp"
#include "oblivrec_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

constexpr std::uint32_t kT = 20;
constexpr double kScale = 1048576.0;   // 2^20

double ToDouble(u64 x) {
  return static_cast<double>(static_cast<std::int64_t>(x)) / kScale;
}
u64 FromDouble(double v) {
  return static_cast<u64>(static_cast<std::int64_t>(v * kScale));
}

// ---- MSNZB ---------------------------------------------------------------
void TestMsnzb() {
  CHECK(MsnzbClear<u64>(0) == -1);
  CHECK(MsnzbClear<u64>(1) == 0);
  CHECK(MsnzbClear<u64>(2) == 1);
  CHECK(MsnzbClear<u64>(3) == 1);
  CHECK(MsnzbClear<u64>(u64(1) << 40) == 40);
  CHECK(MsnzbClear<u64>((u64(1) << 40) | 7) == 40);
  CHECK(MsnzbClear<u64>(~u64(0)) == 63);
  std::printf("  msnzb: correct at 0, 1, powers of two, and the top bit\n");
}

// ---- how many Newton steps does inverse sqrt actually need? -------------
void TestNewtonConvergence() {
  std::mt19937_64 rng(4242);
  std::printf("  inverse sqrt, relative error against std::sqrt:\n");
  std::printf("      %-6s %-12s %-12s %s\n", "steps", "median", "worst",
              "shared rounds");

  int chosen = -1;
  for (int steps = 0; steps <= 6; ++steps) {
    std::vector<double> rel;
    rel.reserve(400);
    for (int t = 0; t < 400; ++t) {
      // Norms squared in the range power iteration actually produces: not
      // tiny, not enormous.
      const double s = 0.01 + static_cast<double>(rng() % 100000) / 1000.0;
      const u64 enc = FromDouble(s);
      if (enc == 0) continue;
      const u64 got = InvSqrtClear<u64>(enc, kT, steps);
      const double want = 1.0 / std::sqrt(s);
      rel.push_back(std::abs(ToDouble(got) - want) / want);
    }
    std::sort(rel.begin(), rel.end());
    const double med = rel[rel.size() / 2];
    const double worst = rel.back();

    int rounds = 0;
    std::vector<u64> v{FromDouble(1.0), FromDouble(2.0)};
    (void)ApproxNormalizeClear<u64>(Span<const u64>(v.data(), v.size()), kT,
                                    steps, &rounds);
    std::printf("      %-6d %-12.2e %-12.2e %d\n", steps, med, worst, rounds);

    // The first step count whose WORST relative error is under 1e-3 is the
    // operating point: power iteration only needs the direction, and a 0.1%
    // error in the length does not move the eigenvector it converges to.
    if (chosen < 0 && worst < 1e-3) chosen = steps;
  }

  CHECK_MSG(chosen > 0,
            "inverse sqrt never reached 1e-3 relative error within 6 Newton "
            "steps; the seed or the fixed-point scale is wrong");
  std::printf("  -> %d Newton steps suffice for <1e-3 relative error. "
              "That is the tunable, now measured.\n", chosen);
}

// ---- the whole normalisation ------------------------------------------
void TestNormalize() {
  std::mt19937_64 rng(77);
  double worst = 0.0;
  const int steps = 4;

  for (int t = 0; t < 200; ++t) {
    const std::size_t n = 4 + static_cast<std::size_t>(rng() % 12);
    std::vector<double> f(n);
    std::vector<u64> v(n);
    for (std::size_t j = 0; j < n; ++j) {
      f[j] = (static_cast<double>(rng() % 20000) / 1000.0) - 10.0;
      v[j] = FromDouble(f[j]);
    }
    auto out = ApproxNormalizeClear<u64>(Span<const u64>(v.data(), n), kT, steps);

    // The result must be a unit vector, which is the only property power
    // iteration needs from it.
    double sq = 0.0;
    for (std::size_t j = 0; j < n; ++j) sq += ToDouble(out[j]) * ToDouble(out[j]);
    const double len = std::sqrt(sq);
    worst = std::max(worst, std::abs(len - 1.0));
  }

  CHECK_MSG(worst < 5e-3,
            "normalised vectors deviated from unit length by up to " +
                std::to_string(worst));
  std::printf("  normalize: 200 random vectors, worst deviation from unit "
              "length = %.2e (%d Newton steps)\n", worst, steps);
}

// ---- headroom, asserted rather than discovered ------------------------
void TestHeadroom() {
  // ||v||^2 at scale t must fit with room for the Newton products, which
  // transiently reach 2t before truncation. At t=20 in a 64-bit ring that
  // leaves 24 bits of magnitude, i.e. |v|^2 < 2^24.
  const int bits_left = 64 - 1 - 2 * static_cast<int>(kT);
  CHECK_MSG(bits_left > 16,
            "only " + std::to_string(bits_left) +
                " magnitude bits remain at 2t; b=64 is too narrow for this t");
  std::printf("  headroom: %d magnitude bits at the transient 2t scale "
              "(b=64, t=%u)\n", bits_left, kT);
}

}  // namespace

int main() {
  std::printf("test_normalize  [CLEARTEXT REFERENCE -- the shared protocol is "
              "not built yet]\n");
  try {
    TestMsnzb();
    TestNewtonConvergence();
    TestNormalize();
    TestHeadroom();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_normalize");
}
