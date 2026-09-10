// ==========================================================================
//  harvest.cpp -- see harvest.hpp for why this closes the loop and what it
//  does and does not hide.
// ==========================================================================
#include "oblivrec/harvest.hpp"

#include <stdexcept>

namespace oblivrec {

template <typename Ring>
void ConsumptionAccumulator<Ring>::Add(Span<const Ring> e) {
  if (e.size() != acc_.size()) {
    throw std::invalid_argument(
        "ConsumptionAccumulator::Add: expected " + std::to_string(acc_.size()) +
        " coefficients, got " + std::to_string(e.size()));
  }
  // Adding shares adds the values they represent. Nothing here is a decision;
  // that is the point -- the accumulation is as cheap as the addition.
  for (std::size_t j = 0; j < acc_.size(); ++j) {
    acc_[j] = static_cast<Ring>(acc_[j] + e[j]);
  }
  ++queries_;
}

template <typename Ring>
SharedVec<Ring> HarvestToReplicated(Mpc3<Ring>& s, Span<const Ring> h0,
                                    Span<const Ring> h1) {
  if (h0.size() != h1.size()) {
    throw std::invalid_argument("HarvestToReplicated: accumulators differ in size");
  }
  const std::size_t n = h0.size();

  // h0 - h1 is the true count (difference convention, dpf.hpp). Writing that
  // as a 3-party additive sharing is arithmetically trivial:
  //     x0 = h0,  x1 = -h1,  x2 = 0   ->   x0 + x1 + x2 = h0 - h1
  //
  // But shipping those values as they stand would be a leak, not a conversion:
  // P2 would receive x0 = h0 and, being told x2 = 0, could subtract its way to
  // the counts as soon as it saw x1. So each is re-randomised with a secret
  // shared between P0 and P1 that P2 does not hold.
  std::vector<Ring> x0(n), x1(n), x2(n, Ring(0));
  for (std::size_t j = 0; j < n; ++j) {
    // The same value on both sides of the pair: party 0's "next" key is party
    // 1's "prev" key. Drawing it from both keeps their counters in step.
    const Ring mask = s.Gen(0).NextPairwise();
    const Ring mask_check = s.Gen(1).PrevPairwise();
    (void)mask_check;
    // P2 draws nothing here, so it must still advance or the three generators
    // desynchronise -- the failure that silently corrupted every
    // multiplication once already.
    s.Gen(2).Skip(1);

    x0[j] = static_cast<Ring>(h0[j] + mask);
    x1[j] = static_cast<Ring>(Ring(0) - h1[j] - mask);
  }

  SharedVec<Ring> out(n);
  for (int i = 0; i < 3; ++i) {
    const std::vector<Ring>* mine = (i == 0) ? &x0 : (i == 1) ? &x1 : &x2;
    const std::vector<Ring>* next = (i == 0) ? &x1 : (i == 1) ? &x2 : &x0;
    for (std::size_t j = 0; j < n; ++j) {
      out.p[static_cast<std::size_t>(i)][j] =
          ReplicatedShare<Ring>((*mine)[j], (*next)[j]);
    }
  }

  // One round: each party's own share plus its neighbour's has to reach it.
  s.AccountRound(3 * n);
  return out;
}

template class ConsumptionAccumulator<u64>;
template class ConsumptionAccumulator<u128>;
template SharedVec<u64> HarvestToReplicated<u64>(Mpc3<u64>&, Span<const u64>,
                                                 Span<const u64>);
template SharedVec<u128> HarvestToReplicated<u128>(Mpc3<u128>&, Span<const u128>,
                                                   Span<const u128>);

}  // namespace oblivrec
