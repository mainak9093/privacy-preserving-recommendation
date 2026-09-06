// ==========================================================================
//  fixedpoint.hpp -- the bridge between the reals and the ring.
//
//  A real v is represented as round(v * 2^t) taken into R = Z_{2^b}, with
//  negatives in two's complement. t = 20 fractional bits, as NUDGE
//  (ARCHITECTURE section 2).
//
//  THIS FILE IS ONE HALF OF A CROSS-LANGUAGE CONTRACT. The other half is
//  model/export.py, which is the ONLY place a float becomes a ring element.
//  The C++ side never sees a float. Endianness, sign extension and t vs 2t
//  scale are the three classic bugs at this boundary, so model/export.py
//  emits a selftest.json of (float, expected int) pairs that
//  tests/test_ring.cpp checks against these functions.
// ==========================================================================
#ifndef OBLIVREC_FIXEDPOINT_HPP
#define OBLIVREC_FIXEDPOINT_HPP

#include <cmath>
#include "oblivrec/ring.hpp"

namespace oblivrec {

constexpr int kFracBits = 20;   // t, per ARCHITECTURE section 2

template <typename Ring>
Ring Encode(double v, int t = kFracBits) {
  const double scaled = std::round(v * static_cast<double>(1ull << t));
  // Cast through the signed twin so negatives land in two's complement.
  using S = typename RingTraits<Ring>::Signed;
  return static_cast<Ring>(static_cast<S>(scaled));
}

template <typename Ring>
double Decode(Ring v, int t = kFracBits) {
  return static_cast<double>(ToSigned<Ring>(v)) / static_cast<double>(1ull << t);
}

}  // namespace oblivrec
#endif  // OBLIVREC_FIXEDPOINT_HPP
