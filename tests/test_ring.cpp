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

// ==========================================================================
//  The CROSS-LANGUAGE half of the fixed-point contract.
//
//  tests/data/fixedpoint_vectors.txt is written by model/export.py and is
//  COMMITTED, so this runs on a fresh clone with no export. It is the only
//  part of the Python-to-C++ boundary testable without a 12 MB model, and the
//  boundary is where endianness, sign extension and t-versus-2t scale errors
//  live.
//
//  A missing file is a FAILURE, not a skip: the file is committed, so its
//  absence means something deleted it.
// ==========================================================================
static void TestCrossLanguageContract() {
  const char* kPath = "tests/data/fixedpoint_vectors.txt";
  std::FILE* fh = std::fopen(kPath, "r");
  if (!fh) {
    CHECK_MSG(false, std::string("cannot open ") + kPath +
                         " (it is committed, so absence is a real failure)");
    return;
  }

  int file_t = -1;
  int checked = 0;
  char line[512];
  while (std::fgets(line, sizeof(line), fh)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

    int t_val = 0;
    if (std::sscanf(line, "t %d", &t_val) == 1) {
      file_t = t_val;
      // The exporter's t must match this build's, or every vector below is
      // scaled wrong and the mismatch would look like an encoding bug.
      CHECK_MSG(file_t == kFracBits,
                "vector file was written at t=" + std::to_string(file_t) +
                    " but this build uses t=" + std::to_string(kFracBits));
      continue;
    }

    double v = 0.0;
    long long want = 0;
    if (std::sscanf(line, "%lf %lld", &v, &want) != 2) continue;

    const u64 got = Encode<u64>(v);
    CHECK_MSG(static_cast<long long>(got) == want,
              "Encode(" + std::to_string(v) + ") gave " +
                  std::to_string(static_cast<long long>(got)) + ", Python said " +
                  std::to_string(want));

    // The 128-bit ring must agree on every value the 64-bit ring represents,
    // since the two share one encoding convention.
    const u128 got128 = Encode<u128>(v);
    CHECK_MSG(static_cast<long long>(static_cast<i128>(got128)) == want,
              "u128 Encode disagrees with u64 at " + std::to_string(v));

    // Byte order: what Python packed with '<q' is what FromBytes must read.
    std::uint8_t buf[8];
    for (int i = 0; i < 8; ++i)
      buf[i] = static_cast<std::uint8_t>((static_cast<std::uint64_t>(want) >> (8 * i)) & 0xff);
    CHECK_MSG(RingTraits<u64>::FromBytes(buf) == got,
              "little-endian round trip failed at " + std::to_string(v));

    ++checked;
  }
  std::fclose(fh);

  CHECK_MSG(file_t == kFracBits, "vector file carried no 't' line");
  CHECK_MSG(checked >= 20,
            "only " + std::to_string(checked) + " vectors checked, expected >= 20");
  std::printf("  cross-language contract: %d vectors agree with Python\n", checked);
}

int main() {
  std::printf("test_ring\n");
  TestRing<u64>("u64");
  TestRing<u128>("u128");
  TestCrossLanguageContract();
  return ::oblivrec_test::Report("test_ring");
}
