// ==========================================================================
//  dpf.hpp -- (2,2)-distributed point function, Boyle-Gilboa-Ishai.
//
//  FROZEN INTERFACE. ARCHITECTURE section 8 makes this header the contract
//  between workstreams, so it is fixed before the body is written. The DPF has
//  two consumers: the PIR read layer in this slice, and the FSS gates that
//  private training will need in Phase 3. One implementation, never forked.
//
//  WHAT IT IS. A point function f_{alpha,beta} is beta at x == alpha and 0
//  everywhere else. A (2,2)-DPF splits f into two keys, each individually
//  hiding alpha and beta, such that the two evaluations recombine to f(x).
//  The keys are logarithmic in the domain, which is the whole point: at
//  domain_bits = 11 a key is ~219 bytes against 16 KB for a naive one-hot
//  vector.
//
//  ------------------------------------------------------------------------
//  THE SIGN CONVENTION, WHICH IS LOAD-BEARING.
//
//  Textbook BGI defines Eval_b = (-1)^b * (Convert(s) + t * CW_last), giving
//  the SUM convention:            Eval_0(x) + Eval_1(x) == f(x).
//
//  This project's specification (ARCHITECTURE section 7.1) instead states the
//  invariant as a DIFFERENCE:
//        EvalFull(k0)[x] - EvalFull(k1)[x] == (x == alpha ? beta : 0)
//
//  Both are correct; they differ only by where the negation lives. We adopt
//  the difference convention by OMITTING the (-1)^b factor and returning
//  Convert(s) + t * CW_last directly. The algebra is identical, because
//    Eval_0 - Eval_1 = (Conv(s_0) - Conv(s_1)) + (t_0 - t_1) * CW_last
//  and CW_last is constructed with exactly the (-1)^{t_1} factor that makes
//  this collapse to beta on the path and 0 off it.
//
//  Getting this backwards makes EVERY test fail in a way that looks like a
//  tree bug rather than a sign bug, which is an expensive place to lose a day.
//  ------------------------------------------------------------------------
//
//  CONTROL BITS are the low bit of each PRG output half, so a seed carries 127
//  effective bits. Standard practice in this literature (Floram, Duoram,
//  Grotto all do it) rather than a shortcut. Documented in the report.
// ==========================================================================
#ifndef OBLIVREC_DPF_HPP
#define OBLIVREC_DPF_HPP

#include <cstdint>
#include <vector>
#include <utility>

#include "oblivrec/prg.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec/span.hpp"

namespace oblivrec {

// One tree level's correction word: lambda + 2 bits.
struct CorrectionWord {
  Block        s;
  std::uint8_t tL = 0;
  std::uint8_t tR = 0;
};

template <typename Ring>
struct DpfKey {
  std::uint8_t  party = 0;          // 0 or 1
  std::uint32_t domain_bits = 0;
  Block         seed;               // this party's INITIAL seed
  std::vector<CorrectionWord> cw;   // one per level, size == domain_bits
  Ring          cw_last = 0;        // the final ring correction

  // Packed wire format, self-describing:
  //   1 (party) + 4 (domain_bits) + 16 (seed)
  //     + domain_bits * (16 seed + 1 tL + 1 tR)
  //     + sizeof(Ring) (cw_last)
  //   = 21 + 18*domain_bits + sizeof(Ring) bytes.
  //
  // At domain_bits = 11 with b = 64 that is 227 bytes, which is the figure the
  // report quotes for the MovieLens-100K demo.
  //
  // An earlier version of this comment said "16 + domain_bits*17 +
  // sizeof(Ring)", which was wrong twice over: it omitted the 5-byte header and
  // counted 17 bytes per level rather than 18. It would have given 211 at
  // domain_bits = 11. SizeBytes() was always right and a test asserts it equals
  // the real serialised length, so nothing was built on the wrong number, but
  // anyone sizing a buffer from the comment would have been 16 bytes short.
  std::vector<std::uint8_t> Serialize() const;
  static DpfKey Deserialize(Span<const std::uint8_t> in);
  std::size_t SizeBytes() const;
};

// Gen returns (k0, k1). Only the CLIENT calls this in S1; no server needs it.
template <typename Ring>
std::pair<DpfKey<Ring>, DpfKey<Ring>> Gen(std::uint32_t alpha,
                                          Ring beta,
                                          std::uint32_t domain_bits);

// Single-point evaluation.
template <typename Ring>
Ring Eval(const DpfKey<Ring>& key, std::uint32_t x);

// Whole-domain evaluation. out.size() must be 1 << domain_bits.
// Depth-first, so working memory is O(domain_bits) blocks, not O(2^domain_bits).
template <typename Ring>
void EvalFull(const DpfKey<Ring>& key, Span<Ring> out);

// --------------------------------------------------------------------------
//  Exposed only for tests. The level-by-level invariant checker in
//  tests/test_dpf_invariant.cpp needs to walk both parties' trees in lockstep
//  and inspect the internal (seed, control bit) state at every node, which is
//  not something a black-box test can do.
// --------------------------------------------------------------------------
namespace detail {

// One tree step, shared by Eval, EvalFull and the test checker so that there
// is a single traversal bug surface rather than three.
// dir = 0 descends left, 1 descends right.
void Traverse(Block* s, std::uint8_t* t, const CorrectionWord& cw, int dir);

// Block -> ring element. Truncates to the low kBits. Sound because the block
// is already pseudorandom and |G| <= 2^lambda.
template <typename Ring>
Ring Convert(const Block& b);

}  // namespace detail
}  // namespace oblivrec
#endif  // OBLIVREC_DPF_HPP
