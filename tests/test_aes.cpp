// ==========================================================================
//  test_aes.cpp -- the AES correctness oracle (RULES.md C3).
//
//  Two layers:
//    1. FIPS-197 Appendix C.1 known-answer test on the scalar reference.
//       This anchors the reference to the published standard, so it is not
//       merely self-consistent.
//    2. 10k random AES-NI vs scalar comparisons. Together these mean that if
//       the DPF misbehaves tomorrow, the PRG is not a candidate explanation.
// ==========================================================================
#include "oblivrec/prg.hpp"
#include "oblivrec_test.hpp"
#include <cstring>
#include <random>

using namespace oblivrec;

static std::string Hex(const std::uint8_t* p, int n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
  return s;
}

int main() {
  std::printf("test_aes\n");

  // ---- 1. FIPS-197 C.1, AES-128 --------------------------------------
  {
    std::uint8_t key[16], pt[16], want[16], got[16];
    for (int i = 0; i < 16; ++i) key[i] = static_cast<std::uint8_t>(i);
    const std::uint8_t pt_v[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                                   0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const std::uint8_t want_v[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
                                     0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    std::memcpy(pt, pt_v, 16);
    std::memcpy(want, want_v, 16);

    AesReferenceEncrypt(key, pt, got);
    CHECK_MSG(std::memcmp(got, want, 16) == 0,
              "scalar got " + Hex(got, 16) + " want " + Hex(want, 16));

    // Same vector through AES-NI.
    AesKeySchedule ks(key);
    __m128i out = ks.Encrypt(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pt)));
    std::uint8_t gotni[16];
    _mm_storeu_si128(reinterpret_cast<__m128i*>(gotni), out);
    CHECK_MSG(std::memcmp(gotni, want, 16) == 0,
              "aesni got " + Hex(gotni, 16) + " want " + Hex(want, 16));
  }

  // ---- 2. AES-NI vs scalar on random inputs ---------------------------
  {
    std::mt19937_64 rng(12345);
    int mismatches = 0;
    for (int trial = 0; trial < 10000; ++trial) {
      std::uint8_t key[16], pt[16], ref[16], ni[16];
      for (int i = 0; i < 16; ++i) {
        key[i] = static_cast<std::uint8_t>(rng() & 0xff);
        pt[i]  = static_cast<std::uint8_t>(rng() & 0xff);
      }
      AesReferenceEncrypt(key, pt, ref);
      AesKeySchedule ks(key);
      __m128i o = ks.Encrypt(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pt)));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(ni), o);
      if (std::memcmp(ref, ni, 16) != 0) ++mismatches;
    }
    CHECK_MSG(mismatches == 0,
              std::to_string(mismatches) + " of 10000 AES-NI/scalar mismatches");
  }

  // ---- 3. Encrypt2 must agree with Encrypt ----------------------------
  {
    std::uint8_t key[16];
    for (int i = 0; i < 16; ++i) key[i] = static_cast<std::uint8_t>(i * 7 + 1);
    AesKeySchedule ks(key);
    std::mt19937_64 rng(999);
    int bad = 0;
    for (int t = 0; t < 1000; ++t) {
      __m128i a = _mm_set_epi64x(static_cast<long long>(rng()), static_cast<long long>(rng()));
      __m128i b = _mm_set_epi64x(static_cast<long long>(rng()), static_cast<long long>(rng()));
      __m128i oa, ob;
      ks.Encrypt2(a, b, &oa, &ob);
      __m128i ra = ks.Encrypt(a), rb = ks.Encrypt(b);
      if (_mm_movemask_epi8(_mm_cmpeq_epi8(oa, ra)) != 0xFFFF) ++bad;
      if (_mm_movemask_epi8(_mm_cmpeq_epi8(ob, rb)) != 0xFFFF) ++bad;
    }
    CHECK_MSG(bad == 0, std::to_string(bad) + " Encrypt2/Encrypt disagreements");
  }

  // ---- 4. The PRG must actually expand ---------------------------------
  {
    // Left and right children must differ from each other and from the seed.
    // A fixed-key permutation without the feed-forward XOR would fail this.
    std::mt19937_64 rng(4242);
    int degenerate = 0;
    for (int t = 0; t < 1000; ++t) {
      std::uint8_t sb[16];
      for (int i = 0; i < 16; ++i) sb[i] = static_cast<std::uint8_t>(rng() & 0xff);
      Block s = Block::FromBytes(sb), l, r;
      Prg().Expand(s, &l, &r);
      if (l == r || l == s || r == s) ++degenerate;
    }
    CHECK_MSG(degenerate == 0,
              std::to_string(degenerate) + " degenerate PRG expansions");
  }

  return ::oblivrec_test::Report("test_aes");
}
