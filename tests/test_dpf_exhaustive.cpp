// ==========================================================================
//  test_dpf_exhaustive.cpp -- the DPF correctness oracle (RULES.md C3).
//
//  The oracle is a brute-force point function: beta at alpha, zero elsewhere.
//  The invariant, exactly as ARCHITECTURE section 7.1 states it:
//
//      Eval(k0, x) - Eval(k1, x)  ==  (x == alpha) ? beta : 0
//
//  checked for EVERY x in the domain and EVERY alpha, for domains small
//  enough to enumerate. "Exhaustive" here means genuinely exhaustive, not
//  sampled: at domain_bits = 12 that is 4096 alphas times 4096 evaluations,
//  which is 16.7M evaluations and still runs in seconds.
//
//  REQUIREMENTS section 7 sets the minimum bar at exhaustive correctness for
//  domain_bits <= 16. Eval covers 1..12 here; EvalFull extends it to 16
//  tomorrow, since it evaluates the whole domain in one pass and is the
//  cheaper way to reach that size.
// ==========================================================================
#include "oblivrec/dpf.hpp"
#include "oblivrec_test.hpp"

#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

template <typename Ring>
bool CheckAlpha(std::uint32_t domain_bits, std::uint32_t alpha, Ring beta) {
  auto kp = Gen<Ring>(alpha, beta, domain_bits);
  const std::uint32_t n = 1u << domain_bits;
  for (std::uint32_t x = 0; x < n; ++x) {
    const Ring got = static_cast<Ring>(Eval<Ring>(kp.first, x) -
                                       Eval<Ring>(kp.second, x));
    const Ring want = (x == alpha) ? beta : Ring(0);
    if (got != want) {
      CHECK_MSG(false,
                "domain_bits=" + std::to_string(domain_bits) +
                " alpha=" + std::to_string(alpha) +
                " x=" + std::to_string(x) +
                " got=" + RingTraits<Ring>::ToDecimal(got) +
                " want=" + RingTraits<Ring>::ToDecimal(want));
      return false;
    }
  }
  return true;
}

// Every alpha, every x. The real thing.
template <typename Ring>
void Exhaustive(const char* ring_name, std::uint32_t max_bits, Ring beta) {
  for (std::uint32_t db = 1; db <= max_bits; ++db) {
    const std::uint32_t n = 1u << db;
    for (std::uint32_t alpha = 0; alpha < n; ++alpha) {
      if (!CheckAlpha<Ring>(db, alpha, beta)) {
        std::printf("  %s: stopped at domain_bits=%u\n", ring_name, db);
        return;
      }
    }
  }
  std::printf("  %s: exhaustive to domain_bits=%u ok\n", ring_name, max_bits);
}

}  // namespace

int main() {
  std::printf("test_dpf_exhaustive\n");

  // Exhaustive over alpha and x, both rings.
  Exhaustive<u64>("u64", 10, 0x0123456789abcdefULL);
  Exhaustive<u128>("u128", 9,
                   (static_cast<u128>(0xfedcba9876543210ULL) << 64) ^
                       0x0123456789abcdefULL);

  // Larger domains: every x, but sampled alpha, so the cost stays linear.
  {
    std::mt19937_64 rng(987654321);
    for (std::uint32_t db = 11; db <= 12; ++db) {
      for (int trial = 0; trial < 4; ++trial) {
        std::uint32_t alpha =
            static_cast<std::uint32_t>(rng() & ((1u << db) - 1));
        CheckAlpha<u64>(db, alpha, static_cast<u64>(rng()));
      }
    }
    std::printf("  u64: domain_bits 11..12, sampled alpha, all x ok\n");
  }

  // Edge cases that off-by-one errors land on.
  CHECK(CheckAlpha<u64>(1, 0, 5));
  CHECK(CheckAlpha<u64>(1, 1, 5));
  CHECK(CheckAlpha<u64>(8, 0, 1));            // alpha at the very start
  CHECK(CheckAlpha<u64>(8, 255, 1));          // alpha at the very end
  CHECK(CheckAlpha<u64>(8, 128, 0));          // beta = 0: everything cancels
  CHECK(CheckAlpha<u64>(8, 77, ~u64(0)));     // beta = -1 in the ring

  // beta must come back EXACTLY, including ring wraparound, because the PIR
  // layer relies on beta = 1 selecting a record with no error term.
  {
    auto kp = Gen<u64>(42, 1, 10);
    const u64 d = static_cast<u64>(Eval<u64>(kp.first, 42) - Eval<u64>(kp.second, 42));
    CHECK_MSG(d == 1, "beta=1 must reconstruct exactly, got " +
                          RingTraits<u64>::ToDecimal(d));
  }

  // Two independent Gen calls on the same (alpha, beta) must produce
  // different keys, or the randomness is not being drawn.
  {
    auto a = Gen<u64>(7, 99, 10);
    auto b = Gen<u64>(7, 99, 10);
    CHECK(!(a.first.seed == b.first.seed));
  }

  return ::oblivrec_test::Report("test_dpf_exhaustive");
}
