// ==========================================================================
//  test_csprng.cpp -- sanity checks on the key-material source.
//
//  These are NOT a proof of cryptographic quality; no test suite can be. They
//  catch the failure modes that actually occur in practice: a generator that
//  returns constants, one that is stuck, one that silently returns fewer bytes
//  than asked, and one that repeats across calls.
//
//  The statistical checks use deliberately loose bounds. The purpose is to
//  catch a generator that is grossly broken, not to fail the build once in
//  every few hundred runs on a healthy one.
// ==========================================================================
#include "oblivrec/csprng.hpp"
#include "oblivrec_test.hpp"

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace oblivrec;

int main() {
  std::printf("test_csprng\n");

  // Not stuck at zero, and the buffer really is written.
  {
    std::uint8_t buf[64];
    std::memset(buf, 0, sizeof(buf));
    RandomBytes(buf, sizeof(buf));
    int nonzero = 0;
    for (unsigned char c : buf) if (c != 0) ++nonzero;
    CHECK_MSG(nonzero > 0, "generator returned all zero bytes");
  }

  // Successive draws must differ. A repeat here would mean DPF key seeds
  // repeat, which is a total break.
  {
    std::set<std::string> seen;
    const int kDraws = 2000;
    for (int i = 0; i < kDraws; ++i) {
      std::uint8_t buf[16];
      RandomBytes(buf, sizeof(buf));
      seen.insert(std::string(reinterpret_cast<char*>(buf), sizeof(buf)));
    }
    CHECK_MSG(static_cast<int>(seen.size()) == kDraws,
              "repeated 128-bit draw in " + std::to_string(kDraws) +
                  " samples, only " + std::to_string(seen.size()) + " distinct");
  }

  // Monobit: over 64 KB the fraction of set bits should sit near one half.
  // Expected stddev of the proportion is about 0.0011 here, so a 0.02 window
  // is roughly 18 sigma and will not flake.
  {
    const std::size_t kBytes = 65536;
    std::vector<std::uint8_t> buf(kBytes);
    RandomBytes(buf.data(), kBytes);
    std::size_t ones = 0;
    for (std::uint8_t c : buf)
      for (int b = 0; b < 8; ++b) ones += (c >> b) & 1u;
    const double frac = static_cast<double>(ones) / (kBytes * 8.0);
    CHECK_MSG(std::fabs(frac - 0.5) < 0.02,
              "monobit fraction " + std::to_string(frac) + " is far from 0.5");
  }

  // Byte-value spread: every one of the 256 values should appear at least once
  // in 64 KB. Catches a generator confined to a subrange.
  {
    const std::size_t kBytes = 65536;
    std::vector<std::uint8_t> buf(kBytes);
    RandomBytes(buf.data(), kBytes);
    bool seen[256] = {false};
    for (std::uint8_t c : buf) seen[c] = true;
    int missing = 0;
    for (bool b : seen) if (!b) ++missing;
    CHECK_MSG(missing == 0,
              std::to_string(missing) + " byte values never appeared in 64 KB");
  }

  // n = 0 must be a well-defined no-op rather than a crash or a stray write.
  {
    std::uint8_t canary[4] = {1, 2, 3, 4};
    RandomBytes(canary, 0);
    CHECK(canary[0] == 1 && canary[1] == 2 && canary[2] == 3 && canary[3] == 4);
  }

  // Odd, non-power-of-two lengths must fill exactly, with no overrun. The
  // guard bytes catch an off-by-one in the chunking loop.
  {
    for (std::size_t n : {1u, 3u, 17u, 31u, 33u, 255u}) {
      std::vector<std::uint8_t> buf(n + 8, 0xAB);
      RandomBytes(buf.data(), n);
      bool guard_intact = true;
      for (std::size_t i = n; i < n + 8; ++i)
        if (buf[i] != 0xAB) guard_intact = false;
      CHECK_MSG(guard_intact,
                "RandomBytes overran the buffer at n=" + std::to_string(n));
    }
  }

  return ::oblivrec_test::Report("test_csprng");
}
