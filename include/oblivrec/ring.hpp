// ==========================================================================
//  ring.hpp -- the ring R = Z_{2^b} and its traits.
//
//  DESIGN NOTE. The ring is a raw unsigned integer type plus a traits class,
//  not a wrapper struct. Unsigned integer + - * in C++ already wrap modulo
//  2^b by definition, which is exactly ring arithmetic, so a wrapper would
//  add friction at every call site and buy nothing.
//
//  b is a TEMPLATE PARAMETER FROM DAY ONE, per ARCHITECTURE section 2, so the
//  b=64 vs b=128 ring-width study (D9.1) costs nothing later.
//
//  b = 64  (dev, and the headline demo)
//  b = 128 (target; NUDGE needs it at Netflix scale because accumulated sums
//           overflow 64 bits. Whether 64 suffices at MovieLens scale is the
//           open question D9.1 asks.)
// ==========================================================================
#ifndef OBLIVREC_RING_HPP
#define OBLIVREC_RING_HPP

#include <cstdint>
#include <cstddef>
#include <string>

namespace oblivrec {

using u64  = std::uint64_t;

// __int128 is a GNU extension, so -Wpedantic objects to it even under
// -std=gnu++17. The extension is load-bearing (it is the b=128 ring, and
// ARCHITECTURE section 2 requires b to be a template parameter), and we want
// -Wpedantic everywhere else, so the diagnostic is suppressed here only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
using u128 = unsigned __int128;
using i128 = __int128;
#pragma GCC diagnostic pop

template <typename T> struct RingTraits;

template <>
struct RingTraits<u64> {
  using Signed = std::int64_t;
  static constexpr int kBits  = 64;
  static constexpr int kBytes = 8;
  static const char* Name() { return "u64"; }

  static u64 FromBytes(const std::uint8_t* p) {          // little-endian
    u64 v = 0;
    for (int i = 0; i < kBytes; ++i) v |= static_cast<u64>(p[i]) << (8 * i);
    return v;
  }
  static void ToBytes(u64 v, std::uint8_t* p) {
    for (int i = 0; i < kBytes; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
  }
  static std::string ToDecimal(u64 v) { return std::to_string(v); }
};

template <>
struct RingTraits<u128> {
  using Signed = i128;
  static constexpr int kBits  = 128;
  static constexpr int kBytes = 16;
  static const char* Name() { return "u128"; }

  static u128 FromBytes(const std::uint8_t* p) {
    u128 v = 0;
    for (int i = 0; i < kBytes; ++i) v |= static_cast<u128>(p[i]) << (8 * i);
    return v;
  }
  static void ToBytes(u128 v, std::uint8_t* p) {
    for (int i = 0; i < kBytes; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
  }
  // __int128 has no operator<< on std::ostream and no std::to_string, so the
  // decimal conversion is hand-rolled. Needed for readable test failures.
  static std::string ToDecimal(u128 v) {
    if (v == 0) return "0";
    char buf[40];
    int i = 39;
    buf[i] = '\0';
    while (v > 0 && i > 0) {
      buf[--i] = static_cast<char>('0' + static_cast<int>(v % 10));
      v /= 10;
    }
    return std::string(buf + i);
  }
};

// Signed interpretation of a ring element, for fixed-point decode.
template <typename Ring>
typename RingTraits<Ring>::Signed ToSigned(Ring v) {
  return static_cast<typename RingTraits<Ring>::Signed>(v);
}

}  // namespace oblivrec
#endif  // OBLIVREC_RING_HPP
