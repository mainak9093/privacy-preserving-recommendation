// ==========================================================================
//  nonlinear.hpp -- Trunc_t and ApproxNormalize (ARCHITECTURE section 4).
//
//  THE ENTIRE INTERACTIVE COST OF TRAINING LIVES HERE. Everything else is
//  either share-times-public (free) or a matrix product whose communication
//  tracks the output vector. So the round count of the whole system is the
//  round count of this file.
//
//  ------------------------------------------------------------------------
//  WHY TRUNCATION IS NEEDED AT ALL.
//
//  Multiplying two t-scaled fixed-point values gives a 2t-scaled result. Left
//  alone, scales grow without bound and overflow the ring -- at b = 64 and
//  t = 20 you get four bits of magnitude after two multiplications. Trunc_t
//  shifts back by t.
//
//  ------------------------------------------------------------------------
//  TWO VARIANTS, AND THE COMPARISON BETWEEN THEM IS A RESULT.
//
//  ARCHITECTURE section 4.1 asks for the improved protocol AND a benchmark
//  against the naive one, because that comparison is self-contained and
//  reportable. Both are here:
//
//    TruncateLocal  Each party shifts its own shares. ZERO rounds, zero
//                   bytes. But it is WRONG in two ways, and only one of them
//                   is small:
//
//                     - low-order carries, discarded independently by each
//                       party, give an error of up to 2 units at the
//                       truncated scale. Bounded and harmless.
//                     - HIGH-ORDER WRAPAROUND. The shares are uniform over
//                       the whole ring, so x0+x1+x2 wraps with high
//                       probability, and a wrap contributes 2^(b-t) to the
//                       result. That is not a rounding error, it is a
//                       catastrophically wrong answer.
//
//                   The measured failure rate is reported by the test rather
//                   than predicted here, because the point of having this
//                   variant is to show what it costs.
//
//    TruncatePair   The standard correct construction: a helper party
//                   supplies a correlated pair (r, r >> t); the masked value
//                   x - r is opened to the other two, truncated in the clear,
//                   and r >> t is added back. Error at most 1 unit, no
//                   wraparound failure. Costs rounds.
//
//  ------------------------------------------------------------------------
//  WHY OPENING x - r IS SAFE HERE, WHICH IS NOT OBVIOUS.
//
//  In 2-of-3 replicated sharing ANY TWO parties reconstruct, so "open to two
//  parties" sounds like a total break. It is not, because the two who see
//  x - r are exactly the two who do NOT know r, and the one who knows r never
//  sees x - r. A single corrupted party therefore holds either r or x - r,
//  never both, and neither alone determines x. This is why the helper role
//  must not be given to a party that also receives the opening -- an easy and
//  fatal mistake, so the code names the helper explicitly.
//
//  ------------------------------------------------------------------------
//  CORRECTNESS DISCIPLINE (section 4, verbatim): both protocols are tested
//  against a cleartext fixed-point oracle over randomised inputs, WITH THE
//  OBSERVED ERROR BOUND RECORDED. An approximate protocol whose error is not
//  measured is not finished.
// ==========================================================================
#ifndef OBLIVREC_NONLINEAR_HPP
#define OBLIVREC_NONLINEAR_HPP

#include <cstdint>
#include <vector>

#include "oblivrec/mpc.hpp"
#include "oblivrec/mvp.hpp"

namespace oblivrec {

// The cleartext oracle both variants are measured against: an arithmetic
// (sign-preserving) right shift of the value the shares represent.
//
// Written here, next to the protocols, rather than in Python. The Python
// oracle earns its place in model/ because Python does the factorisation; for
// a one-line shift it would only re-introduce the float-to-ring boundary that
// has already produced two bugs in this project, for no gain.
template <typename Ring>
Ring TruncateClear(Ring x, std::uint32_t t);

// Zero rounds, and wrong when the shares wrap. Kept as the baseline the
// improved protocol is measured against.
template <typename Ring>
SharedVec<Ring> TruncateLocal(const SharedVec<Ring>& x, std::uint32_t t);

// The correct one. `helper` is the party that generates the pair and must not
// be one of the two that see the opening; passing it explicitly makes the
// requirement visible at every call site.
template <typename Ring>
SharedVec<Ring> TruncatePair(Mpc3<Ring>& s, const SharedVec<Ring>& x,
                             std::uint32_t t, int helper = 2);

// --------------------------------------------------------------------------
//  ApproxNormalize -- CLEARTEXT REFERENCE ONLY, so far.
//
//  ARCHITECTURE section 4.2. The secret-shared protocol is NOT implemented:
//  its seeding step needs b+1 simultaneous FSS integer comparisons, i.e. the
//  comparison gate (old task 2.2) that S1 correctly cut because its only
//  consumers are here. Building that gate is the next piece of work.
//
//  What exists is the cleartext fixed-point reference, written first on
//  purpose: the risk register names ApproxNormalize as the highest-likelihood
//  blocker in the project and names "cleartext oracle first" as the
//  mitigation. Settling the numerics -- Newton step count, error, headroom --
//  before debugging them through secret sharing is the whole point.
//
//  ApproxNormalizeClear also reports the round count the SHARED version will
//  cost, derived from its own structure rather than estimated, so the protocol
//  can be budgeted before it is written.
// --------------------------------------------------------------------------

// Index of the most significant non-zero bit, or -1. A loop here; b+1
// simultaneous FSS comparisons in the protocol.
template <typename Ring>
int MsnzbClear(Ring x);

// Fixed-point inverse square root of a t-scaled positive value.
// `truncations`, if non-null, receives how many Trunc_t calls the shared
// version would need.
template <typename Ring>
Ring InvSqrtClear(Ring s_scaled, std::uint32_t t, int newton_steps,
                  int* truncations = nullptr);

// v / ||v||, in fixed point. `rounds_predicted`, if non-null, receives the
// round count the secret-shared protocol would cost for these parameters.
template <typename Ring>
std::vector<Ring> ApproxNormalizeClear(Span<const Ring> v, std::uint32_t t,
                                       int newton_steps,
                                       int* rounds_predicted = nullptr);

// Truncation as a MatVecProgram stage, so a program's round count includes it.
template <typename Ring>
class TruncStage final : public NonLinear<Ring> {
 public:
  explicit TruncStage(std::uint32_t t, bool exact = true)
      : t_(t), exact_(exact) {}

  SharedVec<Ring> Apply(Mpc3<Ring>& s, const SharedVec<Ring>& v) override {
    return exact_ ? TruncatePair<Ring>(s, v, t_) : TruncateLocal<Ring>(v, t_);
  }
  const char* Name() const override { return exact_ ? "trunc" : "trunc_local"; }
  std::uint32_t DeclaredRounds() const override { return exact_ ? 3u : 0u; }

 private:
  std::uint32_t t_;
  bool exact_;
};

}  // namespace oblivrec
#endif  // OBLIVREC_NONLINEAR_HPP
