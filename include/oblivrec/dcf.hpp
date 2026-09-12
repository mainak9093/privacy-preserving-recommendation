// ==========================================================================
//  dcf.hpp -- the distributed COMPARISON function (task 2.2, due here).
//
//      f^<_{alpha,beta}(x) = beta  if x < alpha,  else 0
//
//  Cut from S1 on 2026-09-06 because its only consumers are Trunc_t and
//  ApproxNormalize, both of which are S2. They are S2 now, so it comes due.
//
//  ------------------------------------------------------------------------
//  WHY THIS IS NOT THE DPF WITH A DIFFERENT PAYLOAD.
//
//  The design draft asserted that a DPF and a DCF are "the same primitive".
//  They are not, and the reference implementation of NUDGE keeps them in
//  separate modules, which is what prompted checking. A DPF is non-zero at ONE
//  point; a DCF is non-zero on a whole PREFIX of the domain, and the
//  construction has to accumulate a payload across every subtree that lies
//  entirely to the left of alpha's path.
//
//  Concretely, this needs three things the DPF does not:
//
//    1. FOUR PRG outputs per node, not two: each direction carries a seed AND
//       a value word. Hence Expand4 in prg.hpp.
//    2. A per-level VALUE correction word, on top of the seed and control
//       correction words the DPF already has.
//    3. A running accumulator V_alpha in Gen, which is what makes the
//       construction subtle -- each level's correction depends on the sum of
//       everything below it.
//
//  So the key is bigger: 16 + domain_bits * (16 + 2 + sizeof(Ring)) + ...
//  rather than the DPF's 18 bytes per level.
//
//  ------------------------------------------------------------------------
//  SIGN CONVENTION, AND WHY IT DIFFERS INTERNALLY.
//
//  The DCF construction is defined with a (-1)^party factor and gives the SUM
//  convention: Eval_0(x) + Eval_1(x) = f(x). This project uses the DIFFERENCE
//  convention everywhere else (dpf.hpp), and mixing the two would be exactly
//  the kind of sign confusion that makes a failure look like a tree bug.
//
//  Resolved by CONVERTING AT THE BOUNDARY rather than rewriting the
//  construction: the internal evaluation is faithful to the paper, and the
//  public EvalDcf negates party 1's output. Then
//
//      EvalDcf(k0, x) - EvalDcf(k1, x) == f(x)
//
//  matching the DPF, so a caller never has to remember which primitive it is
//  holding. Recorded in the Decisions Log.
// ==========================================================================
#ifndef OBLIVREC_DCF_HPP
#define OBLIVREC_DCF_HPP

#include <cstdint>
#include <utility>
#include <vector>

#include "oblivrec/prg.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec/span.hpp"

namespace oblivrec {

template <typename Ring>
struct DcfCorrectionWord {
  Block        s;
  Ring         v = 0;      // the value correction the DPF has no analogue for
  std::uint8_t tL = 0;
  std::uint8_t tR = 0;
};

template <typename Ring>
struct DcfKey {
  std::uint8_t  party = 0;
  std::uint32_t domain_bits = 0;
  Block         seed;
  std::vector<DcfCorrectionWord<Ring>> cw;   // one per level
  Ring          cw_last = 0;

  std::vector<std::uint8_t> Serialize() const;
  static DcfKey Deserialize(Span<const std::uint8_t> in);
  std::size_t SizeBytes() const;
};

// Keys for f^<_{alpha,beta}. Only the client calls this, as with the DPF.
//
// The domain index is u128, not u64. The FSS gate in msnzb.hpp masks a value
// with kappa extra bits of randomness before opening it, so its domain is
// value_bits + kappa + 1 -- which passes 64 for any realistic value range.
// Capping the index at 64 bits would silently cap the achievable statistical
// security, which is the wrong thing to trade away by accident.
template <typename Ring>
std::pair<DcfKey<Ring>, DcfKey<Ring>> GenDcf(std::uint32_t domain_bits,
                                             u128 alpha, Ring beta);

// One party's share of f(x). DIFFERENCE convention: see the header comment.
template <typename Ring>
Ring EvalDcf(const DcfKey<Ring>& key, u128 x);

// Every point of the domain, which is what an FSS gate needs. Costs one tree
// walk rather than 2^domain_bits of them.
template <typename Ring>
void EvalFullDcf(const DcfKey<Ring>& key, Span<Ring> out);

}  // namespace oblivrec
#endif  // OBLIVREC_DCF_HPP
