// ==========================================================================
//  test_dcf.cpp -- the comparison function, verified the way the DPF was.
//
//  EXHAUSTIVELY, because a DCF that is right at most points and wrong on one
//  subtree is exactly what a direction error produces, and a sampled test
//  would miss it. Every alpha against every x, for every domain up to the
//  bound -- which is the same standard tests/test_dpf_exhaustive.cpp holds the
//  DPF to, and this primitive is no less load-bearing now that
//  ApproxNormalize depends on it.
// ==========================================================================
#include "oblivrec/dcf.hpp"
#include "oblivrec_test.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

// Exhaustive over one domain: every alpha, every x.
template <typename Ring>
bool SweepDomain(std::uint32_t db, Ring beta, const char* ring_name) {
  const std::uint64_t n = std::uint64_t(1) << db;
  for (std::uint64_t alpha = 0; alpha < n; ++alpha) {
    auto keys = GenDcf<Ring>(db, alpha, beta);
    for (std::uint64_t x = 0; x < n; ++x) {
      // DIFFERENCE convention, matching the DPF (see dcf.hpp).
      const Ring got = static_cast<Ring>(EvalDcf<Ring>(keys.first, x) -
                                         EvalDcf<Ring>(keys.second, x));
      const Ring want = (x < alpha) ? beta : Ring(0);
      if (got != want) {
        std::printf("  FAIL %s db=%u alpha=%llu x=%llu: expected %s\n",
                    ring_name, db, (unsigned long long)alpha,
                    (unsigned long long)x,
                    (x < alpha) ? "beta" : "zero");
        return false;
      }
    }
  }
  return true;
}

template <typename Ring>
void TestExhaustive(const char* ring_name, std::uint32_t max_bits) {
  std::mt19937_64 rng(20260912);
  for (std::uint32_t db = 1; db <= max_bits; ++db) {
    // A different beta per domain: a single fixed value would test the sign
    // handling at exactly one point, which is the mistake the DPF sweep had to
    // be corrected for on 2026-09-06.
    const Ring beta = static_cast<Ring>(rng() | 1u);
    const bool ok = SweepDomain<Ring>(db, beta, ring_name);
    CHECK_MSG(ok, std::string(ring_name) + ": exhaustive sweep failed at db=" +
                      std::to_string(db));
    if (!ok) return;
  }
  const std::uint64_t checks = (std::uint64_t(1) << (2 * max_bits + 1));
  std::printf("  %-4s exhaustive to db=%u: every alpha x every x, ~%llu leaf "
              "checks, all correct\n", ring_name, max_bits,
              (unsigned long long)checks / 3);
}

// The edge cases that a uniform sweep still would not emphasise.
void TestEdges() {
  // alpha = 0: nothing is below it, so the function is identically zero.
  {
    auto k = GenDcf<u64>(6, 0, u64(12345));
    for (std::uint64_t x = 0; x < 64; ++x) {
      const u64 got = static_cast<u64>(EvalDcf<u64>(k.first, x) -
                                       EvalDcf<u64>(k.second, x));
      CHECK_MSG(got == 0, "alpha=0 must give the all-zero function");
    }
  }
  // alpha at the top: everything below it is below it.
  {
    const std::uint64_t top = 63;
    auto k = GenDcf<u64>(6, top, u64(7));
    int ones = 0;
    for (std::uint64_t x = 0; x < 64; ++x) {
      const u64 got = static_cast<u64>(EvalDcf<u64>(k.first, x) -
                                       EvalDcf<u64>(k.second, x));
      if (got == 7) ++ones;
    }
    CHECK_MSG(ones == 63, "alpha=2^db-1 should be non-zero at 63 points, got " +
                              std::to_string(ones));
  }
  // beta = 1, which is what the MSNZB gate actually uses.
  {
    auto k = GenDcf<u64>(5, 19, u64(1));
    for (std::uint64_t x = 0; x < 32; ++x) {
      const u64 got = static_cast<u64>(EvalDcf<u64>(k.first, x) -
                                       EvalDcf<u64>(k.second, x));
      CHECK(got == (x < 19 ? u64(1) : u64(0)));
    }
  }
  std::printf("  edges: alpha=0, alpha=max, beta=1 all behave\n");
}

// A single party's share must look like nothing.
void TestHiding() {
  auto k = GenDcf<u64>(8, 137, u64(1));
  int zeros = 0, ones = 0;
  for (std::uint64_t x = 0; x < 256; ++x) {
    const u64 s = EvalDcf<u64>(k.first, x);
    if (s == 0) ++zeros;
    if (s == 1) ++ones;
  }
  CHECK_MSG(zeros <= 1 && ones <= 1,
            "one party's DCF shares take the plaintext values 0/1 too often "
            "(" + std::to_string(zeros) + " zeros, " + std::to_string(ones) +
            " ones); they must be pseudorandom");
  std::printf("  hiding: one party's shares are pseudorandom, not 0/1\n");
}

void TestSerialisation() {
  auto k = GenDcf<u64>(9, 300, u64(0xDEADBEEF));
  const auto bytes = k.first.Serialize();
  CHECK_MSG(bytes.size() == k.first.SizeBytes(),
            "SizeBytes disagrees with the serialised length");
  auto back = DcfKey<u64>::Deserialize(
      Span<const std::uint8_t>(bytes.data(), bytes.size()));
  for (std::uint64_t x = 0; x < 512; x += 7) {
    CHECK(EvalDcf<u64>(back, x) == EvalDcf<u64>(k.first, x));
  }

  // Malformed input must be rejected, not misread.
  bool threw = false;
  auto bad = bytes;
  bad[0] = 7;
  try { (void)DcfKey<u64>::Deserialize(Span<const std::uint8_t>(bad.data(), bad.size())); }
  catch (const std::invalid_argument&) { threw = true; }
  CHECK_MSG(threw, "a party byte of 7 was accepted");

  threw = false;
  try {
    (void)DcfKey<u64>::Deserialize(
        Span<const std::uint8_t>(bytes.data(), bytes.size() - 1));
  } catch (const std::invalid_argument&) { threw = true; }
  CHECK_MSG(threw, "a truncated key was accepted");

  std::printf("  serialise: %zu B at db=9, round-trips, rejects malformed\n",
              bytes.size());
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("test_dcf\n");
  // Same env override the DPF sweep uses, so `make check` can run this at a
  // reduced bound under hardening without a separate knob.
  std::uint32_t max_bits = 7;
  if (const char* e = std::getenv("OBLIVREC_MAX_BITS")) {
    max_bits = static_cast<std::uint32_t>(std::atoi(e));
    if (max_bits < 1 || max_bits > 12) max_bits = 7;
  }
  if (argc > 1) max_bits = static_cast<std::uint32_t>(std::atoi(argv[1]));

  TestExhaustive<u64>("u64", max_bits);
  TestExhaustive<u128>("u128", max_bits > 6 ? 6 : max_bits);
  TestEdges();
  TestHiding();
  TestSerialisation();
  return ::oblivrec_test::Report("test_dcf");
}
