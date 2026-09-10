// ==========================================================================
//  mf.hpp -- private matrix factorisation (W3, ARCHITECTURE section 5).
//
//  The cleartext reference is model/mf.py, written to mirror the protocol's
//  STRUCTURE rather than call numpy.linalg.svd, precisely so this comparison
//  is structural and not merely numerical.
//
//  ------------------------------------------------------------------------
//  SetOrthogonal NEEDS NO MULTIPLICATION PROTOCOL. Stated carefully, because
//  the obvious stronger claim is false.
//
//  ApproxFactor Gram-Schmidts each candidate against the rows of B already
//  found:  v := v - sum_i <v, B_i> B_i
//
//  In NUDGE each converged row of B is OPENED before the next component is
//  computed, so every B_i here is PUBLIC. Therefore:
//
//      <v, B_i>        shared . public              -> LOCAL
//      <v, B_i> * B_i  shared scalar x public vec   -> LOCAL
//      v - (...)       share subtraction            -> LOCAL
//
//  Not one shared-by-shared product appears, so not one round is spent on
//  arithmetic. That is why the design can afford d components without d rounds
//  of orthogonalisation, and it is a large part of why NUDGE opens B at all.
//
//  ------------------------------------------------------------------------
//  BUT IT IS NOT FREE, AND SAYING SO WOULD BE AN OVERCLAIM.
//
//  Fixed point makes the scales grow. With inputs at t fractional bits:
//
//      v            2^t
//      <v, B_i>     2^2t     (a product of two t-scaled values)
//      <v,B_i>*B_i  2^3t     (another one)
//
//  so the projection comes out at 3t while v is at t. Bringing them back into
//  a common scale is TRUNCATION, and truncation is interactive. The rounds
//  this operation costs are truncation rounds, not multiplication rounds.
//
//  This function therefore does the LOCAL part only and is explicit about the
//  scale it returns. Deciding WHEN to pay for the rescale is the
//  deferred-truncation schedule (PHASES task 3.6), which owns that judgement
//  globally rather than per call -- deferring is the whole point, and a
//  function that silently truncated on its own would defeat it.
//
//  HEADROOM. Returning at 3t is only safe if the ring has room: at b = 64 and
//  t = 20 that leaves 4 bits of magnitude, which is NOT enough for general
//  use. Callers must either truncate promptly or run at b = 128. The bound is
//  computed by ScaleHeadroomBits() so it can be asserted at startup rather
//  than discovered as silent corruption -- which is exactly the failure mode
//  the risk register lists as medium-likelihood and high-impact.
// ==========================================================================
#ifndef OBLIVREC_MF_HPP
#define OBLIVREC_MF_HPP

#include <cstdint>
#include <vector>

#include "oblivrec/mpc.hpp"

namespace oblivrec {

// Result of the local Gram-Schmidt, with its scale carried explicitly so a
// caller cannot lose track of it.
template <typename Ring>
struct ScaledVec {
  SharedVec<Ring> v;
  std::uint32_t frac_bits = 0;    // v represents  value * 2^frac_bits
};

// Gram-Schmidt a shared vector against `rows_filled` already-converged PUBLIC
// rows of B (row-major, `n` columns, each entry t-scaled).
//
// LOCAL: takes no Mpc3& because it needs none. The absent session parameter is
// the interface stating the cost.
//
// `v` is at `frac_bits`; the result is at `frac_bits + 2t_b`, where t_b is the
// scale of B's entries. Both v and the projection are lifted to that common
// scale before subtraction, so no information is discarded here.
template <typename Ring>
ScaledVec<Ring> SetOrthogonalPublic(const ScaledVec<Ring>& v,
                                    Span<const Ring> B,
                                    std::uint32_t rows_filled, std::uint32_t n,
                                    std::uint32_t b_frac_bits);

// Bits of magnitude left in the ring at a given fractional scale. Assert this
// is positive and comfortable BEFORE running, not after the results look odd.
template <typename Ring>
constexpr int ScaleHeadroomBits(std::uint32_t frac_bits) {
  return static_cast<int>(RingTraits<Ring>::kBits) - 1 -
         static_cast<int>(frac_bits);
}

// The same operation when B's rows are still SHARED -- which this design never
// needs. Provided so the cost difference can be MEASURED rather than asserted:
// it spends one round per row where the public version spends none.
template <typename Ring>
SharedVec<Ring> SetOrthogonalShared(Mpc3<Ring>& s, const SharedVec<Ring>& v,
                                    const SharedMatrix<Ring>& B,
                                    std::uint32_t rows_filled);

}  // namespace oblivrec
#endif  // OBLIVREC_MF_HPP
