// ==========================================================================
//  normalize.cpp -- ApproxNormalize, cleartext reference (ARCHITECTURE 4.2).
//
//  ------------------------------------------------------------------------
//  WHAT IS HERE, AND WHAT IS NOT. Read this before using it.
//
//  This file implements the CLEARTEXT fixed-point reference for
//  ApproxNormalize, structured line for line the way the secret-shared
//  protocol is. It is NOT the protocol.
//
//  This paragraph said "the protocol is not built yet" until 2026-09-24. It
//  was built on 2026-09-12 -- msnzb.cpp's gate and InvSqrtShared, driven by
//  FssNormalizer. What this file is FOR is unchanged and is the reason it
//  still exists: it is the oracle the protocol is checked against. A protocol
//  with no cleartext twin is a protocol whose errors look like bugs and whose
//  bugs look like errors.
//
//  It is written first, deliberately. ARCHITECTURE section 4 requires both
//  non-linear protocols to be measured against a cleartext oracle with the
//  observed error bound recorded, and the project's own risk register lists
//  ApproxNormalize as the highest-likelihood blocker with "cleartext oracle
//  first" as the mitigation. Doing it in this order means the numerical
//  behaviour -- how many Newton steps, how much error, how much headroom -- is
//  settled BEFORE any of it has to be debugged through secret sharing.
//
//  ------------------------------------------------------------------------
//  THE ALGORITHM, and why it is shaped like this.
//
//  Power iteration renormalises v every step or it overflows. That needs
//  1/||v||, i.e. an inverse square root, which is the awkward one under MPC.
//
//    ||v||^2   degree two -> one round, cheap.
//    seed      2^(-floor(log2 ||v||^2)/2) ~ 1/||v||. Obtained from the
//              most-significant-non-zero-bit. In the protocol this is b+1
//              SIMULTANEOUS FSS comparisons -- one round, no extra leakage --
//              which is the trick that avoids an O(b)-round loop or a giant
//              lookup table.
//    refine    Newton-Raphson, y <- y(3 - s y^2)/2. Each step roughly doubles
//              the correct digits, so a CONSTANT number suffices. How many is
//              a tunable, and this file is what measures it rather than
//              guessing.
//
//  ------------------------------------------------------------------------
//  EVERY TRUNCATION IS COUNTED, because that is the protocol's round cost.
//
//  In the shared version each Trunc_t below becomes a real 3-round protocol
//  and each product becomes a 1-round multiplication. So the cleartext code
//  reports TruncationsPerStep() and the test turns that into a predicted round
//  count for the protocol that does not exist yet. That prediction is the
//  useful output of writing the oracle first.
// ==========================================================================
#include "oblivrec/nonlinear.hpp"

#include <stdexcept>

namespace oblivrec {

// Most significant non-zero bit index, or -1 for zero.
//
// The protocol obtains this from simultaneous FSS comparisons against every
// power of two at once. Here it is a loop, and the loop is fine BECAUSE this
// is cleartext -- writing it as a loop in the protocol would cost b rounds and
// is precisely what section 4.2 is avoiding.
template <typename Ring>
int MsnzbClear(Ring x) {
  int msb = -1;
  for (int i = 0; i < RingTraits<Ring>::kBits; ++i) {
    if ((x >> i) & Ring(1)) msb = i;
  }
  return msb;
}

template <typename Ring>
Ring InvSqrtClear(Ring s_scaled, std::uint32_t t, int newton_steps,
                  int* truncations) {
  using S = typename RingTraits<Ring>::Signed;
  if (static_cast<S>(s_scaled) <= 0) {
    throw std::invalid_argument("InvSqrtClear: argument must be positive");
  }
  int trunc_count = 0;

  // ---- seed from the MSNZB -------------------------------------------------
  // s_scaled = s * 2^t, so s ~ 2^(k - t) where k is the msnzb index, and
  // 1/sqrt(s) ~ 2^(-(k-t)/2). Scaled by 2^t that is 2^((3t - k)/2).
  const int k = MsnzbClear<Ring>(s_scaled);
  const int shift = (3 * static_cast<int>(t) - k) / 2;
  Ring y = (shift >= 0 && shift < RingTraits<Ring>::kBits)
               ? static_cast<Ring>(Ring(1) << shift)
               : Ring(1);

  // ---- Newton-Raphson: y <- y (3 - s y^2) / 2 ------------------------------
  const Ring three = static_cast<Ring>(Ring(3) << t);
  for (int step = 0; step < newton_steps; ++step) {
    // sy = s * y, back to scale t
    Ring sy = TruncateClear<Ring>(static_cast<Ring>(s_scaled * y), t);
    ++trunc_count;
    // sy2 = s * y^2, back to scale t
    Ring sy2 = TruncateClear<Ring>(static_cast<Ring>(sy * y), t);
    ++trunc_count;
    // (3 - s y^2) is a subtraction at scale t: free, no truncation.
    const Ring corr = static_cast<Ring>(three - sy2);
    // y * corr, back to scale t, then halve. The halving is an exact shift of
    // a public constant, so it is not a truncation in the protocol sense.
    y = TruncateClear<Ring>(static_cast<Ring>(y * corr), t);
    ++trunc_count;
    y = static_cast<Ring>(static_cast<S>(y) >> 1);
  }
  if (truncations) *truncations = trunc_count;
  return y;
}

template <typename Ring>
std::vector<Ring> ApproxNormalizeClear(Span<const Ring> v, std::uint32_t t,
                                       int newton_steps, int* rounds_predicted) {
  const std::size_t n = v.size();

  // ||v||^2 at scale 2t, truncated back to t. In the protocol this is one
  // InnerProduct (1 round) plus one Trunc_t (3 rounds).
  Ring acc = 0;
  for (std::size_t j = 0; j < n; ++j) {
    acc = static_cast<Ring>(acc + v[j] * v[j]);
  }
  const Ring norm2 = TruncateClear<Ring>(acc, t);

  int trunc_in_invsqrt = 0;
  const Ring inv = InvSqrtClear<Ring>(norm2, t, newton_steps, &trunc_in_invsqrt);

  std::vector<Ring> out(n);
  for (std::size_t j = 0; j < n; ++j) {
    out[j] = TruncateClear<Ring>(static_cast<Ring>(v[j] * inv), t);
  }

  if (rounds_predicted) {
    // What the SHARED version would cost, from the structure above:
    //   1  InnerProduct                      (1 round)
    //   1  Trunc_t on ||v||^2                (3 rounds)
    //   1  MSNZB via simultaneous FSS        (1 round)
    //   per Newton step: 3 muls + 3 Trunc_t  (3 + 9 rounds)
    //   1  scaling multiply + Trunc_t        (1 + 3 rounds)
    const int per_step = 3 /*mul*/ + 3 * 3 /*trunc*/;
    *rounds_predicted = 1 + 3 + 1 + newton_steps * per_step + 1 + 3;
  }
  (void)trunc_in_invsqrt;
  return out;
}

template int MsnzbClear<u64>(u64);
template int MsnzbClear<u128>(u128);
template u64 InvSqrtClear<u64>(u64, std::uint32_t, int, int*);
template u128 InvSqrtClear<u128>(u128, std::uint32_t, int, int*);
template std::vector<u64> ApproxNormalizeClear<u64>(Span<const u64>,
                                                    std::uint32_t, int, int*);
template std::vector<u128> ApproxNormalizeClear<u128>(Span<const u128>,
                                                      std::uint32_t, int, int*);

}  // namespace oblivrec
