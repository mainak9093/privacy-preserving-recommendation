// ==========================================================================
//  harvest.hpp -- closing the PIRSONA loop (task 3.9, ARCHITECTURE 7.3).
//
//  This is what makes the system a CYCLE rather than two halves bolted
//  together, and it is the part of the composition neither base paper has:
//  NUDGE trains but delegates fetching, PIRSONA fetches but its training core
//  is superseded. Here the output of delivery feeds the input of training.
//
//  ------------------------------------------------------------------------
//  THE OBSERVATION, AND WHY IT COSTS ALMOST NOTHING.
//
//  PirServer::Answer already computes EvalFull(k_c) across the whole domain --
//  a share of the one-hot indicator e_alpha -- and then throws it away after
//  the inner product. But that vector IS the consumption record: it says, in
//  shared form, which item this user just fetched.
//
//  So the servers can accumulate it. Over a user's queries the running sum is
//  a shared count of what they consumed, ready to be the next round's training
//  input, with NO separate upload step and NO extra communication. The
//  expansion was already paid for.
//
//  ------------------------------------------------------------------------
//  THE SHARING CHANGES SHAPE, AND THAT IS THE REAL WORK.
//
//  The two PIR servers hold DPF shares, and this project uses the difference
//  convention (dpf.hpp): EvalFull(k0)[j] - EvalFull(k1)[j] is 1 at alpha and 0
//  elsewhere. So after accumulating, P0 and P1 hold h0 and h1 with
//
//      h0 - h1 = the true consumption vector
//
//  which is a 2-out-of-2 additive sharing between two parties. Training needs
//  2-out-of-3 replicated sharing across three. Converting is a real protocol
//  step, not a cast, and it must RE-RANDOMISE: handing P2 the raw h0 would let
//  it subtract its way to the answer once it also saw h1.
//
//  ------------------------------------------------------------------------
//  WHAT THIS DOES AND DOES NOT HIDE.
//
//  Hidden: which items were fetched. The accumulator is a share; neither
//  server learns anything from its own copy.
//
//  NOT hidden, and worth saying plainly: HOW MANY queries a user made. The
//  accumulator is updated once per query, so the query count is visible to
//  each server, exactly as the threat model already records ("message timing
//  and query counts" are leaked by design). Harvesting adds no new leak on top
//  of what running the PIR layer already reveals -- which is precisely why it
//  is free.
// ==========================================================================
#ifndef OBLIVREC_HARVEST_HPP
#define OBLIVREC_HARVEST_HPP

#include <cstdint>
#include <vector>

#include "oblivrec/mpc.hpp"
#include "oblivrec/pir.hpp"

namespace oblivrec {

// One server's running consumption record for one user: a share of the sum of
// the one-hot vectors of everything that user fetched.
template <typename Ring>
class ConsumptionAccumulator {
 public:
  explicit ConsumptionAccumulator(std::uint32_t domain_size)
      : acc_(domain_size, Ring(0)) {}

  // Fold in one query's expanded coefficients. `e` is EvalFull's output, which
  // PirServer::AnswerAndHarvest hands over rather than discarding.
  void Add(Span<const Ring> e);

  Span<const Ring> Shares() const {
    return Span<const Ring>(acc_.data(), acc_.size());
  }
  std::uint64_t Queries() const { return queries_; }

 private:
  std::vector<Ring> acc_;
  std::uint64_t queries_ = 0;
};

// ---------------------------------------------------------------------------
//  THE WEIGHT CHECK (D9.3). Added 2026-09-24, after finding the hole.
//
//  THE HOLE. An honest client goes through PirClient::Query, which fixes
//  beta = 1 so the difference of the two expansions is the selector vector
//  exactly. A malicious client does not have to: it can call Gen(alpha, beta,
//  domain_bits) directly with ANY beta. Retrieval still works for it -- the
//  record comes back scaled by beta and it divides that out -- and the servers
//  fold beta, not 1, into the consumption accumulator that becomes the next
//  round's training input. Every existing check passes. Gen(alpha, 10^6, 11)
//  buys a million-weight vote in the next model.
//
//  THE CHECK. Sum the expansion over the whole domain. Summing is LINEAR, so
//  each server can do it locally on its own share, and
//
//      sum_j e0[j] - sum_j e1[j]  ==  beta
//
//  exactly, for any alpha. So the two servers open that single scalar and
//  require it to be 1. One round, two ring elements per query.
//
//  OPENING IT LEAKS NOTHING. The secret this system protects is alpha -- WHICH
//  item was fetched. beta is a payload that is supposed to be the public
//  constant 1, and the sum is independent of alpha by construction. Each
//  server's own sum is pseudorandom; their difference is beta and nothing else.
//
//  WHAT IT DOES NOT CLOSE, stated because the boundary matters. The sum bounds
//  the TOTAL weight, not its distribution. A client that forged correction
//  words directly -- rather than calling Gen -- could produce a difference
//  vector of +2 at one index and -1 at another, summing to 1 and passing this
//  check while still skewing two items. Proving a key encodes a genuine
//  one-point function is what a Sabre-style audit does, and that is D9.4's
//  stretch half, which we have not built. This check closes weight INFLATION,
//  which is the cheap and total version of the attack; it does not close
//  weight REDISTRIBUTION.
// ---------------------------------------------------------------------------

// The weight a single query actually carried. `e0` and `e1` are one query's
// two expansions. Returns beta as a signed value: 1 for an honest query.
template <typename Ring>
typename RingTraits<Ring>::Signed HarvestWeight(Span<const Ring> e0,
                                                Span<const Ring> e1);

// The check itself, charged one round. Returns true iff the query carried
// weight exactly 1. A rejected query must be dropped BEFORE it reaches the
// accumulator, which is why this takes the raw expansions rather than the
// accumulated state -- once it is folded in, it cannot be taken out.
template <typename Ring>
bool HarvestWeightOk(Mpc3<Ring>& s, Span<const Ring> e0, Span<const Ring> e1);

// Convert the two PIR servers' accumulators into a 2-of-3 replicated sharing
// the training pipeline can consume.
//
// `h0` and `h1` satisfy h0 - h1 = the true counts (difference convention). The
// conversion re-randomises with a pairwise secret P2 does not hold, so no
// single party learns the counts -- without that step this would be a leak
// dressed as a type conversion.
//
// Costs one round: the re-randomised shares have to reach their neighbours.
template <typename Ring>
SharedVec<Ring> HarvestToReplicated(Mpc3<Ring>& s, Span<const Ring> h0,
                                    Span<const Ring> h1);

}  // namespace oblivrec
#endif  // OBLIVREC_HARVEST_HPP
