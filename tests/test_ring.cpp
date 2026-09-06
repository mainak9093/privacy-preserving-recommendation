// ==========================================================================
//  test_ring.cpp -- ring arithmetic and the fixed-point bridge.
//
//  The cross-LANGUAGE half of this contract (against model/export.py's
//  selftest.json) lands on day 4. This file covers the C++ side alone:
//  byte round-trips, two's-complement negatives, and wraparound.
// ==========================================================================
#include "oblivrec/ring.hpp"
#include "oblivrec/fixedpoint.hpp"
#include "oblivrec_test.hpp"
#include <random>
#include <vector>

using namespace oblivrec;

template <typename Ring>
static void TestRing(const char* name) {
  using RT = RingTraits<Ring>;
  std::printf("  ring %s (%d bits)\n", name, RT::kBits);

  // Byte round-trip, little-endian.
  std::mt19937_64 rng(7);
  for (int t = 0; t < 2000; ++t) {
    Ring v = static_cast<Ring>(rng());
    if (RT::kBits > 64) v = (v << 64) ^ static_cast<Ring>(rng());
    std::uint8_t buf[16] = {0};
    RT::ToBytes(v, buf);
    CHECK(RT::FromBytes(buf) == v);
  }

  // Wraparound is the ring operation, so it must be exact.
  {
    Ring max = static_cast<Ring>(~static_cast<Ring>(0));
    CHECK(static_cast<Ring>(max + 1) == static_cast<Ring>(0));
    CHECK(static_cast<Ring>(static_cast<Ring>(0) - 1) == max);
  }

  // Fixed point: positives, negatives and zero must survive a round trip
  // within one quantum of 2^-t.
  {
    const double quantum = 1.0 / static_cast<double>(1ull << kFracBits);
    const double vals[] = {0.0, 1.0, -1.0, 0.5, -0.5, 3.14159, -2.71828,
                           1e-4, -1e-4, 123.456, -123.456};
    for (double v : vals) {
      Ring e = Encode<Ring>(v);
      double back = Decode<Ring>(e);
      CHECK_MSG(std::abs(back - v) <= quantum,
                std::string("fixedpoint round trip failed for ") + std::to_string(v) +
                " got " + std::to_string(back));
    }
    // A negative must have its top bit set, i.e. it is two's complement in
    // the ring rather than a saturating or absolute-value encoding.
    Ring neg = Encode<Ring>(-1.0);
    CHECK(ToSigned<Ring>(neg) < 0);
    CHECK(Decode<Ring>(neg) < 0.0);
  }

  // Additive homomorphism: encoding is linear, which is what makes shares of
  // scores reconstruct correctly.
  {
    const double quantum = 1.0 / static_cast<double>(1ull << kFracBits);
    std::mt19937_64 r2(11);
    for (int t = 0; t < 1000; ++t) {
      double a = (static_cast<double>(r2() % 20000) - 10000.0) / 100.0;
      double b = (static_cast<double>(r2() % 20000) - 10000.0) / 100.0;
      Ring sum = static_cast<Ring>(Encode<Ring>(a) + Encode<Ring>(b));
      CHECK(std::abs(Decode<Ring>(sum) - (a + b)) <= 2 * quantum);
    }
  }

  // ToDecimal must not crash and must be right for small values, since it is
  // the only way a u128 failure prints readably.
  CHECK(RT::ToDecimal(static_cast<Ring>(0)) == "0");
  CHECK(RT::ToDecimal(static_cast<Ring>(1234567890)) == "1234567890");
}

int main() {
  std::printf("test_ring\n");
  TestRing<u64>("u64");
  TestRing<u128>("u128");
  return ::oblivrec_test::Report("test_ring");
}
