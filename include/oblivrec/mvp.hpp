// ==========================================================================
//  mvp.hpp -- the matrix-vector program abstraction.
//
//  ARCHITECTURE section 3.3, following NUDGE Def 4.1:
//
//      P(v, M_1 ... M_l) := f_l(M_l . ... f_2(M_2 . f_1(M_1 . v)))
//
//  It exists because it separates the free part from the interactive part and
//  makes the round count obvious by inspection rather than by tracing calls.
//
//  ------------------------------------------------------------------------
//  A CORRECTION TO THE DESIGN DRAFT.
//
//  Section 3.3's code sketch comments the run method with
//  "rounds == number of NonLinear stages". That is NOT right in general, and
//  the distinction matters because round count is what dominates on a WAN:
//
//    - a stage whose matrix is PUBLIC costs zero rounds (the map is local);
//    - a stage whose matrix is SHARED costs one round by itself, before any
//      non-linear stage runs.
//
//  So rounds == (number of SHARED matrices) + (rounds of each NonLinear).
//  DeclaredRounds() computes that, Run() measures what actually happened, and
//  the test asserts the two agree. The draft's claim holds only in the special
//  case where every matrix is public, which is S1's serving path -- and that
//  is very likely where the sentence came from.
// ==========================================================================
#ifndef OBLIVREC_MVP_HPP
#define OBLIVREC_MVP_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "oblivrec/mpc.hpp"

namespace oblivrec {

// A non-linear stage: anything that is not a matrix product. Truncation and
// normalisation are the only two this system needs, which is the whole reason
// training is affordable.
template <typename Ring>
class NonLinear {
 public:
  virtual ~NonLinear() = default;
  virtual SharedVec<Ring> Apply(Mpc3<Ring>& s, const SharedVec<Ring>& v) = 0;
  virtual const char* Name() const = 0;
  // What this stage costs, declared up front so a program's round count can be
  // computed without running it. Checked against the measured count by a test.
  virtual std::uint32_t DeclaredRounds() const = 0;
};

// The do-nothing stage, so a program can have a matrix product with no
// non-linearity after it without a special case.
template <typename Ring>
class NoOp final : public NonLinear<Ring> {
 public:
  SharedVec<Ring> Apply(Mpc3<Ring>&, const SharedVec<Ring>& v) override {
    return v;
  }
  const char* Name() const override { return "noop"; }
  std::uint32_t DeclaredRounds() const override { return 0; }
};

template <typename Ring>
class MatVecProgram {
 public:
  // A stage with a SHARED matrix. Costs one round for the product itself.
  void Push(const SharedMatrix<Ring>& M, std::unique_ptr<NonLinear<Ring>> f);

  // A stage with a PUBLIC matrix. The product is local and costs nothing;
  // only the non-linear stage can charge a round. Kept as a separate entry
  // point rather than a flag so the cost difference is visible at the call
  // site, which is where someone reasoning about rounds is looking.
  void PushPublic(std::vector<Ring> M, std::uint32_t rows, std::uint32_t cols,
                  std::unique_ptr<NonLinear<Ring>> f);

  // Round count computed from the stages, without running anything.
  std::uint32_t DeclaredRounds() const;

  // Run the program. Rounds actually taken are `s.Rounds()` before and after.
  SharedVec<Ring> Run(Mpc3<Ring>& s, const SharedVec<Ring>& v) const;

  std::size_t Stages() const { return stages_.size(); }
  std::string Describe() const;

 private:
  struct Stage {
    bool shared = true;
    SharedMatrix<Ring> shared_m;
    std::vector<Ring> public_m;
    std::uint32_t rows = 0, cols = 0;
    std::shared_ptr<NonLinear<Ring>> f;
  };
  std::vector<Stage> stages_;
};

}  // namespace oblivrec
#endif  // OBLIVREC_MVP_HPP
