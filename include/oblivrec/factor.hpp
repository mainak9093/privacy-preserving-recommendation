// ==========================================================================
//  factor.hpp -- ApproxFactor under secret sharing (ARCHITECTURE section 5),
//  and the deferred-truncation schedule that keeps it inside the ring.
//
//      B := 0
//      for i in 1..d:
//          v := random n-vector
//          v := Normalize(SetOrthogonal(v, B))
//          for j in 1..ell:
//              v := U^T (U v)            two matrix-vector products
//              v := SetOrthogonal(v, B)  Gram-Schmidt vs PUBLIC rows: local
//              v := Normalize(v)         interactive
//          B[i] := v                     <- REVEALED IN THE CLEAR
//      A := U . B^T
//
//  ------------------------------------------------------------------------
//  THE NORMALIZER IS PLUGGABLE, AND THAT IS NOT A CONVENIENCE.
//
//  ApproxNormalize as specified needs b+1 simultaneous FSS comparisons, i.e.
//  the comparison gate that S1 cut because its only consumers are here. It is
//  not built. Rather than block all of ApproxFactor on it, or quietly
//  substitute something weaker, the normalisation step is an interface with
//  two implementations whose names state what they cost:
//
//    FssNormalizer         spec-faithful. NOT BUILT -- throws, with the
//                          reason. This is the one that belongs in the final
//                          system.
//
//    RevealNormNormalizer  opens ||v||^2 and rescales by a public constant.
//                          Works today, costs 2 rounds instead of ~57, and
//                          **CHANGES THE LEAKAGE PROFILE**: it reveals one
//                          scalar per normalisation, which across a run is
//                          the trajectory of the singular values of U.
//
//  THE SECOND IS NOT THE DEFAULT AND MUST BE PASSED EXPLICITLY. Section 5's
//  pseudocode normalises BEFORE revealing B[i], so the unit vector is public
//  and the norm is not; using RevealNorm departs from that. Whether the
//  departure is acceptable is a threat-model question -- d aggregate spectral
//  values over all users, against ~57 rounds per normalisation -- and it is
//  recorded as an open decision rather than settled here.
//
//  ------------------------------------------------------------------------
//  SCALES, and why U is kept at scale ZERO.
//
//  Ratings are small integers (1..5 on MovieLens). Encoding them at t
//  fractional bits would spend 20 bits of headroom to represent a value that
//  has no fractional part. So U stays at scale 0 and only v carries the
//  fixed-point scale:
//
//      U        scale 0        v        scale t
//      U v      scale t        U^T(U v) scale t     -> NO truncation needed
//
//  That is a real saving: the two matrix products in the hot loop, which are
//  the expensive part, need no truncation at all. Truncation is needed only
//  after SetOrthogonal, which lands at 3t.
//
//  ------------------------------------------------------------------------
//  DEFERRED TRUNCATION (task 3.6). Section 5: truncate by 2t after every other
//  multiplication rather than t after each, halving the truncation count at
//  the cost of headroom. The safe schedule depends on b, so it is DERIVED from
//  public parameters and ASSERTED AT STARTUP -- silent fixed-point overflow is
//  listed in the risk register as medium-likelihood and high-impact, and an
//  assertion at startup is the difference between a crash and a wrong model.
// ==========================================================================
#ifndef OBLIVREC_FACTOR_HPP
#define OBLIVREC_FACTOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "oblivrec/mf.hpp"
#include "oblivrec/mpc.hpp"
#include "oblivrec/nonlinear.hpp"

namespace oblivrec {

struct FactorParams {
  std::uint32_t m = 0;        // users
  std::uint32_t n = 0;        // items
  std::uint32_t d = 16;       // components
  std::uint32_t ell = 10;     // inner power-iteration rounds
  std::uint32_t t = 20;       // fractional bits
  std::uint32_t max_rating = 5;
  // Number of non-zero ratings. PUBLIC: the threat model already lists the
  // count of non-zero entries as leaked by design, so using it in a bound
  // reveals nothing new -- and it gives a far tighter bound than m*n does.
  std::uint64_t nnz = 0;
};

// --------------------------------------------------------------------------
//  Task 3.6: where truncations go, and proof that the ring can take it.
// --------------------------------------------------------------------------
class TruncationSchedule {
 public:
  // Spare bits demanded before a schedule is considered safe. The growth
  // bound is worst-case over public parameters, not a guarantee about a
  // particular matrix, so "fits exactly" is not good enough.
  static constexpr int kMinHeadroomBits = 8;

  // Derive a safe schedule, or throw explaining why none exists. `ring_bits`
  // is b. Deriving rather than hardcoding is the point: the answer changes
  // with n, d, t and b, and the b=64 vs b=128 study (D9.1) is exactly the
  // question of where this stops having an answer.
  static TruncationSchedule Derive(const FactorParams& p, int ring_bits);

  // Whether the DEFERRED schedule (peak 3t, half as many truncations) fits.
  // When it does not, the schedule falls back to truncating immediately at 2t
  // and says so -- which is the honest answer to "does b=64 survive", i.e.
  // question D9.1.
  bool Deferred() const { return deferred_; }

  // Bits to shift v by right after the two matrix products, to bring its
  // magnitude back before ||v||^2 squares it. Derived from the public growth
  // bound, so it is data-independent.
  std::uint32_t PostMatVecShift() const { return post_matvec_shift_; }

  // Peak scale reached before a truncation brings it back.
  std::uint32_t PeakFracBits() const { return peak_frac_; }
  // How much each truncation removes. 2t under the deferred schedule.
  std::uint32_t TruncBits() const { return trunc_bits_; }
  // Magnitude bits left at the peak. Must be positive, and comfortably so.
  int HeadroomBits() const { return headroom_; }
  // Worst-case magnitude of U^T U v, in bits, from public parameters only.
  int GrowthBits() const { return growth_; }

  // Call before running. Throws with the numbers if the ring is too narrow.
  void AssertHeadroom() const;
  std::string Explain() const;

 private:
  std::uint32_t peak_frac_ = 0, trunc_bits_ = 0, post_matvec_shift_ = 0;
  int headroom_ = 0, growth_ = 0, ring_bits_ = 0;
  bool deferred_ = false;
};

// --------------------------------------------------------------------------
//  The normalisation step. See the header comment on why this is an interface.
// --------------------------------------------------------------------------
template <typename Ring>
class Normalizer {
 public:
  virtual ~Normalizer() = default;
  // Normalise a t-scaled shared vector in place-ish, returning the result.
  virtual SharedVec<Ring> Apply(Mpc3<Ring>& s, const SharedVec<Ring>& v,
                                std::uint32_t t) = 0;
  virtual const char* Name() const = 0;
  // Whether this implementation reveals the norm. Recorded per run so a
  // result can never be reported without its leakage.
  virtual bool RevealsNorm() const = 0;
  // Norms this normalizer has revealed so far, for exactly that reason.
  virtual const std::vector<double>& Revealed() const = 0;
};

// Spec-faithful. NOT BUILT: needs the FSS comparison gate.
template <typename Ring>
class FssNormalizer final : public Normalizer<Ring> {
 public:
  SharedVec<Ring> Apply(Mpc3<Ring>&, const SharedVec<Ring>&,
                        std::uint32_t) override;
  const char* Name() const override { return "fss (not built)"; }
  bool RevealsNorm() const override { return false; }
  const std::vector<double>& Revealed() const override { return empty_; }

 private:
  std::vector<double> empty_;
};

// Works today, and LEAKS ONE SCALAR PER CALL. Named so that cannot be missed.
template <typename Ring>
class RevealNormNormalizer final : public Normalizer<Ring> {
 public:
  SharedVec<Ring> Apply(Mpc3<Ring>& s, const SharedVec<Ring>& v,
                        std::uint32_t t) override;
  const char* Name() const override { return "reveal-norm (LEAKS ||v||)"; }
  bool RevealsNorm() const override { return true; }
  const std::vector<double>& Revealed() const override { return revealed_; }

 private:
  std::vector<double> revealed_;
};

// --------------------------------------------------------------------------
//  The result, including what it cost and what it leaked.
// --------------------------------------------------------------------------
template <typename Ring>
struct FactorResult {
  std::vector<Ring> B;                  // d x n, PUBLIC, t-scaled, row-major
  std::uint64_t rounds = 0;
  std::uint64_t bytes = 0;
  std::uint64_t truncations = 0;
  std::vector<double> revealed_norms;   // empty iff the normalizer leaks none
  std::string normalizer;
};

// U is shared, m x n, at scale 0 (raw ratings). Returns B revealed in the
// clear, as section 5 specifies.
template <typename Ring>
FactorResult<Ring> ApproxFactorShared(Mpc3<Ring>& s, const SharedMatrix<Ring>& U,
                                      const FactorParams& p,
                                      const TruncationSchedule& sched,
                                      Normalizer<Ring>& norm,
                                      std::uint64_t seed = 0);

// The same computation in cleartext fixed point, with the SAME truncation
// points, so the shared version can be checked against something that shares
// its numerics rather than against floating point.
template <typename Ring>
std::vector<Ring> ApproxFactorClear(Span<const Ring> U, const FactorParams& p,
                                    std::uint64_t seed = 0);

}  // namespace oblivrec
#endif  // OBLIVREC_FACTOR_HPP
