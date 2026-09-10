// ==========================================================================
//  mpc.hpp -- the 3PC substrate: correlated randomness and multiplication.
//
//  ARCHITECTURE section 3. This is what S1 deliberately did NOT need: serving
//  is a public matrix applied to a shared vector, which is a local linear map.
//  Training multiplies two SHARED values, and that costs a round.
//
//  ------------------------------------------------------------------------
//  THE SHARING. x = x0 + x1 + x2, party i holds (x_i, x_{i+1}) -- exactly what
//  share.hpp's ReplicatedShare{lo, hi} already stores, with lo = x_i and
//  hi = x_{i+1}. Nothing here changes that representation.
//
//  ------------------------------------------------------------------------
//  THE "3PC WITH PRF" MODEL, and why multiplication is cheap.
//
//  Each PAIR of parties shares a PRF key. Party i holds the key it shares with
//  i+1 and the key it shares with i-1, so it can compute
//
//      alpha_i = F(k_i, ctr) - F(k_{i-1}, ctr)
//
//  with NO COMMUNICATION, and alpha_0 + alpha_1 + alpha_2 = 0 by construction:
//  every term appears once positively and once negatively. These zero-shares
//  let a party re-randomise a locally computed product without revealing it.
//
//  Multiplication (Araki et al.), for z = x*y:
//
//      z_i = x_i*y_i + x_i*y_{i+1} + x_{i+1}*y_i + alpha_i     -- local
//      party i sends z_i to party i-1                          -- ONE element
//
//  Summing the three z_i telescopes to (x0+x1+x2)(y0+y1+y2) = x*y, because
//  every cross term x_a*y_b appears exactly once across the three parties.
//  After the exchange party i holds (z_i, z_{i+1}) -- the replicated form
//  again, so multiplications compose.
//
//  COST: one round, one ring element per party. THIS FILE IS THE ONLY PLACE IN
//  THE SYSTEM THAT COMMUNICATES DURING TRAINING, so counting rounds is the
//  same as counting calls into it.
//
//  ------------------------------------------------------------------------
//  WHY ALL THREE PARTIES LIVE IN ONE PROCESS HERE.
//
//  S1 already demonstrated the protocol across three real processes over TCP
//  (channel.hpp, and the distinguisher experiment). Re-doing that here would
//  re-test the transport rather than the arithmetic. So this substrate holds
//  all three parties' state and performs the exchange as an assignment -- but
//  it COUNTS the rounds and bytes it would have sent, so the cost model is
//  measured rather than asserted, and a test checks the accounting.
// ==========================================================================
#ifndef OBLIVREC_MPC_HPP
#define OBLIVREC_MPC_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "oblivrec/prg.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec/share.hpp"
#include "oblivrec/span.hpp"

namespace oblivrec {

// A vector held in shared form by all three parties. `p[i]` is party i's view.
// ARCHITECTURE section 3.3 names this type; it is one object rather than three
// loose vectors so a protocol cannot accidentally mix parties up.
template <typename Ring>
struct SharedVec {
  std::array<std::vector<ReplicatedShare<Ring>>, 3> p;

  SharedVec() = default;
  explicit SharedVec(std::size_t n) { for (auto& v : p) v.assign(n, {}); }

  std::size_t size() const { return p[0].size(); }
  bool Consistent() const {
    return p[0].size() == p[1].size() && p[1].size() == p[2].size();
  }
};

// A row-major shared matrix. Stored flat; `rows * cols == data.size()`.
template <typename Ring>
struct SharedMatrix {
  SharedVec<Ring> data;
  std::uint32_t rows = 0, cols = 0;
};

// One party's view of the pairwise-PRF correlated randomness.
//
// Party i is given the key it shares with i+1 ("next") and with i-1 ("prev").
// Both members of a pair step the same counter, so the two evaluations of a
// shared key agree and cancel around the ring of three.
template <typename Ring>
class ZeroShareGen {
 public:
  ZeroShareGen(const std::uint8_t next_key[16], const std::uint8_t prev_key[16])
      : next_(next_key), prev_(prev_key) {}

  // alpha_i at the current counter, then advance. Summed over the three
  // parties at the same counter this is exactly zero.
  Ring Next();

  // A pseudorandom value shared with the NEXT party only; the neighbour gets
  // the same value from PrevPairwise(). Used where two parties must agree on a
  // secret the third does not learn -- which is exactly what re-randomising a
  // resharing needs, so the helper cannot invert it.
  Ring NextPairwise();
  Ring PrevPairwise();

  // Advance without output, so a party that skips a draw cannot silently
  // desynchronise the ring.
  void Skip(std::uint64_t n = 1) { ctr_ += n; }
  std::uint64_t Counter() const { return ctr_; }

 private:
  static Ring Eval(const AesKeySchedule& k, std::uint64_t ctr);

  AesKeySchedule next_, prev_;
  std::uint64_t ctr_ = 0;
};

// The three-party session: correlated randomness plus cost accounting.
//
// Rounds and bytes are counted rather than estimated, because NUDGE Thm 4.2 --
// communication scales with the largest intermediate VECTOR, not with the
// input matrices -- is a claim about these counters, and tests/test_mpc.cpp
// asserts it.
template <typename Ring>
class Mpc3 {
 public:
  explicit Mpc3(std::uint64_t seed = 0);

  // ---- the operations that communicate ---------------------------------

  // Element-wise product. ONE round whatever n is, because every element rides
  // in the same message.
  SharedVec<Ring> MulVec(const SharedVec<Ring>& x, const SharedVec<Ring>& y);

  // Inner product. One round, and ONE ring element per party rather than n:
  // the summation happens locally BEFORE the exchange. This asymmetry is the
  // whole reason the design pushes work onto the non-interactive side.
  // Returned as a length-1 SharedVec rather than one party's share, so the
  // result is a first-class shared value that composes with everything else.
  SharedVec<Ring> InnerProduct(const SharedVec<Ring>& x,
                               const SharedVec<Ring>& y);

  // Shared matrix times shared vector. One round; communication is `rows`
  // elements per party -- the size of the OUTPUT, not of the matrix.
  SharedVec<Ring> MatVec(const SharedMatrix<Ring>& M, const SharedVec<Ring>& v);

  // ---- free operations, here so the contrast is visible ----------------

  // Public matrix times shared vector: entirely local, zero rounds. This is
  // what S1's serving path does, and it is why serving needs no protocol.
  static SharedVec<Ring> MatVecPublic(Span<const Ring> M, std::uint32_t rows,
                                      std::uint32_t cols,
                                      const SharedVec<Ring>& v);

  // Inner product against a PUBLIC vector: local, zero rounds.
  static SharedVec<Ring> InnerProductPublic(const SharedVec<Ring>& x,
                                            Span<const Ring> pub);

  // ---- accounting -------------------------------------------------------
  std::uint64_t Rounds() const { return rounds_; }
  std::uint64_t BytesSent() const { return bytes_; }
  void ResetCounters() { rounds_ = 0; bytes_ = 0; }

  ZeroShareGen<Ring>& Gen(int p) { return *gens_[static_cast<std::size_t>(p)]; }

  // Randomness known to ONE party only. A deployment draws this from the OS
  // CSPRNG (csprng.hpp, as DPF key material already does); here it is seeded
  // from the session seed so a failing protocol test reproduces exactly.
  Ring PrivateRandom(int party);

  // Charge the accounting from a protocol implemented outside this class.
  // `elements` is the total number of ring elements crossing the wire in this
  // round, counted across all parties. Protocols call this rather than
  // touching the counters, so no operation can quietly under-report itself.
  void AccountRound(std::size_t elements) {
    ++rounds_;
    bytes_ += static_cast<std::uint64_t>(elements) *
              static_cast<std::uint64_t>(RingTraits<Ring>::kBytes);
  }

 private:
  // Every communicating operation funnels through here, so none can forget to
  // account for itself.
  SharedVec<Ring> Exchange(const std::array<std::vector<Ring>, 3>& z);

  std::array<std::unique_ptr<ZeroShareGen<Ring>>, 3> gens_;
  std::array<std::uint64_t, 3> priv_state_{};
  std::uint64_t rounds_ = 0;
  std::uint64_t bytes_ = 0;
};

// --------------------------------------------------------------------------
//  Building and opening shared values, for drivers and tests.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> SplitVec(Span<const Ring> values);

template <typename Ring>
std::vector<Ring> OpenVec(const SharedVec<Ring>& v);

}  // namespace oblivrec
#endif  // OBLIVREC_MPC_HPP
