// ==========================================================================
//  msnzb.hpp -- the FSS comparison gate, and the ApproxNormalize seed.
//
//  This is the piece ARCHITECTURE 4.2 calls "b+1 SIMULTANEOUS integer
//  comparisons using FSS -- one round, no extra leakage", and it is what
//  RevealNormNormalizer was standing in for.
//
//  ------------------------------------------------------------------------
//  HOW AN FSS GATE EVALUATES A SECRET INPUT.
//
//  A DCF key hides its threshold alpha and is evaluated at a PUBLIC point. We
//  need the opposite: a secret input against public thresholds. The standard
//  inversion is mask-and-reveal:
//
//      offline   a dealer draws r and builds DCF keys with alpha_k = 2^k + r
//      online    parties open c = S + r, then each evaluates its keys at c
//
//  and since 1[S < 2^k] == 1[S + r < 2^k + r] when neither sum wraps, the
//  evaluation at the public c yields shares of the comparison on the secret S.
//  Every threshold is evaluated at the SAME c, which is why all of them cost
//  ONE round together rather than one round each.
//
//  MSNZB then telescopes:  sum over k of 1[S >= 2^k]  ==  msnzb(S) + 1,
//  and any step function of msnzb -- in particular the inverse-sqrt seed
//  2^((3t-k)/2) -- is a linear combination of those same indicators. So one
//  gate gives the seed directly, with no second round.
//
//  ------------------------------------------------------------------------
//  THE CONSTRAINT THAT DECIDES THE RING WIDTH, and it is a new D9.1 result.
//
//  Opening c = S + r hides S only if r is drawn from a range much wider than
//  S's. With S in [0, 2^L) and a statistical security parameter kappa, r must
//  come from [0, 2^(L+kappa)), so c lives in L + kappa + 1 bits and the DCF
//  domain must be at least that wide:
//
//      domain_bits >= L + kappa + 1
//
//  At kappa = 40 and the value ranges power iteration actually produces
//  (L ~ 40 for ||v||^2 at t = 20), that is ~81 bits.
//
//      b = 64   CANNOT support a statistically hiding MSNZB gate at these
//               ranges. There is no room for the mask.
//      b = 128  fits comfortably.
//
//  That is a sharper answer to D9.1 than the headroom study alone gave. The
//  arithmetic headroom study said b=64 survives to ML-1M but loses the
//  deferred schedule; this says that the moment you want a SPEC-FAITHFUL
//  normaliser rather than one that reveals the norm, b=64 stops being an
//  option at all. The two studies constrain the ring width for different
//  reasons and the tighter one wins.
// ==========================================================================
#ifndef OBLIVREC_MSNZB_HPP
#define OBLIVREC_MSNZB_HPP

#include <cstdint>
#include <vector>

#include "oblivrec/dcf.hpp"
#include "oblivrec/mpc.hpp"

namespace oblivrec {

// Statistical security for the mask. 40 bits is the usual choice for this kind
// of masking argument and is what the width budget above assumes.
constexpr std::uint32_t kMaskKappa = 40;

// One gate instance, covering thresholds 2^lo .. 2^hi. Restricting the range
// to what the value can actually occupy is what keeps the key count small:
// the general gate needs b+1 comparisons, this one needs hi-lo+1.
template <typename Ring>
class MsnzbGate {
 public:
  // Build the offline material. `lo`/`hi` bound msnzb(S); a value outside is a
  // programming error, not a silent wrong answer, so Apply checks.
  MsnzbGate(Mpc3<Ring>& s, std::uint32_t lo, std::uint32_t hi,
            std::uint32_t value_bits);

  // Shares of a step function of msnzb(S): `table[k - lo]` is the value the
  // result takes when msnzb(S) == k. One round.
  SharedVec<Ring> Apply(Mpc3<Ring>& s, const SharedVec<Ring>& S,
                        const std::vector<Ring>& table) const;

  std::uint32_t DomainBits() const { return domain_bits_; }
  std::uint32_t Lo() const { return lo_; }
  std::uint32_t Hi() const { return hi_; }
  std::size_t KeyBytes() const;

 private:
  std::uint32_t lo_ = 0, hi_ = 0, domain_bits_ = 0;
  u128 r_ = 0;                                // the dealer's mask
  std::vector<DcfKey<Ring>> k0_, k1_;         // one pair per threshold
};

// Shares of 1/sqrt(S) at scale t, using the gate for the seed and
// Newton-Raphson to refine. This is ApproxNormalize's hard half.
template <typename Ring>
SharedVec<Ring> InvSqrtShared(Mpc3<Ring>& s, const SharedVec<Ring>& S,
                              std::uint32_t t, int newton_steps,
                              const MsnzbGate<Ring>& gate);

}  // namespace oblivrec
#endif  // OBLIVREC_MSNZB_HPP
