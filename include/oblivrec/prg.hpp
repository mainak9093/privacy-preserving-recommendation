// ==========================================================================
//  prg.hpp -- the length-doubling PRG that drives the DPF's GGM tree.
//
//  CONSTRUCTION (ARCHITECTURE section 7.1): AES-128 in fixed-key
//  Matyas-Meyer-Oseas / Davies-Meyer form, sigma(x) = pi(x) XOR x, with
//  AES-NI. One 128-bit seed expands to two child seeds plus two control bits.
//
//  WHY FIXED-KEY. The DPF evaluates the PRG once per tree level per point, so
//  the key schedule must not be recomputed per call. Fixing two keys at static
//  init turns each expansion into ten AESENC instructions. This is what
//  Floram, Duoram and Grotto all do.
//
//  WHY THE XOR. Raw AES with a fixed public key is a permutation, and a
//  permutation is not a PRG (it is invertible, so the child seeds would be
//  recoverable from each other). The feed-forward XOR breaks invertibility
//  and gives the standard MMO compression function.
//
//  CONTROL BITS are the low bit of each output half, so the seed carries 127
//  effective bits rather than 128. This is standard practice in the DPF
//  literature rather than a shortcut, and it is documented in the report.
// ==========================================================================
#ifndef OBLIVREC_PRG_HPP
#define OBLIVREC_PRG_HPP

#include <immintrin.h>
#include <cstdint>
#include <cstring>

namespace oblivrec {

struct alignas(16) Block {
  __m128i v;

  Block() : v(_mm_setzero_si128()) {}
  explicit Block(__m128i x) : v(x) {}

  static Block Zero() { return Block(); }
  static Block FromBytes(const std::uint8_t b[16]) {
    Block r;
    r.v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
    return r;
  }
  void ToBytes(std::uint8_t b[16]) const {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(b), v);
  }
  // Low bit of the block, used as the GGM control bit.
  std::uint8_t Lsb() const {
    return static_cast<std::uint8_t>(_mm_cvtsi128_si64(v) & 1);
  }
  Block operator^(const Block& o) const { return Block(_mm_xor_si128(v, o.v)); }
  bool operator==(const Block& o) const {
    return _mm_movemask_epi8(_mm_cmpeq_epi8(v, o.v)) == 0xFFFF;
  }
  bool operator!=(const Block& o) const { return !(*this == o); }
};

// AES-128 round keys, expanded once.
class AesKeySchedule {
 public:
  explicit AesKeySchedule(const std::uint8_t key[16]);
  __m128i Encrypt(__m128i x) const;
  // Two independent inputs interleaved. AESENC has ~4-cycle latency and
  // 1-cycle throughput, so running two chains together is close to free.
  void Encrypt2(__m128i a, __m128i b, __m128i* oa, __m128i* ob) const;

 private:
  __m128i rk_[11];
};

// The length-doubling PRG: seed -> (left, right).
class FixedKeyPrg {
 public:
  FixedKeyPrg();
  void Expand(const Block& seed, Block* left, Block* right) const;

 private:
  AesKeySchedule k0_, k1_;
};

// The one shared instance. Keys are compile-time constants, so both parties
// and both servers agree without any setup message.
const FixedKeyPrg& Prg();

// Scalar AES-128 reference, for the correctness oracle only. Never on a hot
// path. Exists so tests/test_aes.cpp can cross-check the AES-NI path.
void AesReferenceEncrypt(const std::uint8_t key[16],
                         const std::uint8_t in[16],
                         std::uint8_t out[16]);

}  // namespace oblivrec
#endif  // OBLIVREC_PRG_HPP
