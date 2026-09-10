// ==========================================================================
//  msnzb.cpp -- the FSS comparison gate. See msnzb.hpp, especially on why the
//  mask width decides the ring width.
// ==========================================================================
#include "oblivrec/msnzb.hpp"

#include <cmath>
#include <stdexcept>

#include "oblivrec/csprng.hpp"
#include "oblivrec/nonlinear.hpp"

namespace oblivrec {

template <typename Ring>
MsnzbGate<Ring>::MsnzbGate(Mpc3<Ring>& s, std::uint32_t lo, std::uint32_t hi,
                           std::uint32_t value_bits)
    : lo_(lo), hi_(hi) {
  if (hi < lo) throw std::invalid_argument("MsnzbGate: hi < lo");

  // The domain has to hold the masked value: value_bits for S, kappa more for
  // the mask, and one for the carry.
  domain_bits_ = value_bits + kMaskKappa + 1;
  if (domain_bits_ > 127) {
    throw std::invalid_argument(
        "MsnzbGate: needs " + std::to_string(domain_bits_) +
        " domain bits, beyond what a DCF domain supports.");
  }
  if (static_cast<int>(domain_bits_) > RingTraits<Ring>::kBits - 1) {
    throw std::overflow_error(
        "MsnzbGate: a statistically hiding gate needs " +
        std::to_string(domain_bits_) + " bits but the ring has only " +
        std::to_string(RingTraits<Ring>::kBits) +
        ". THIS IS THE D9.1 ANSWER for the spec-faithful normaliser: b=64 "
        "cannot hold the mask at these value ranges; b=128 can.");
  }

  // The dealer's mask. In deployment this is preprocessing done by a party
  // that does not see the opening -- the same role split Trunc_t uses. Here it
  // is drawn from the OS CSPRNG, like all other key material in this project.
  std::uint8_t buf[16];
  RandomBytes(buf, sizeof(buf));
  u128 r = 0;
  for (int i = 0; i < 16; ++i) r |= u128(buf[i]) << (8 * i);
  const std::uint32_t mask_bits = value_bits + kMaskKappa;
  r_ = (mask_bits >= 128) ? r : (r & ((u128(1) << mask_bits) - 1));

  // One DCF per threshold. alpha_k = 2^k + r, so evaluating at c = S + r
  // decides S < 2^k.
  for (std::uint32_t k = lo_; k <= hi_; ++k) {
    const u128 alpha = (u128(1) << k) + r_;
    auto pair = GenDcf<Ring>(domain_bits_, alpha, Ring(1));
    k0_.push_back(pair.first);
    k1_.push_back(pair.second);
  }
}

template <typename Ring>
std::size_t MsnzbGate<Ring>::KeyBytes() const {
  return k0_.empty() ? 0 : k0_.size() * k0_[0].SizeBytes() * 2;
}

template <typename Ring>
SharedVec<Ring> MsnzbGate<Ring>::Apply(Mpc3<Ring>& s, const SharedVec<Ring>& S,
                                       const std::vector<Ring>& table) const {
  using Sg = typename RingTraits<Ring>::Signed;
  const std::size_t count = hi_ - lo_ + 1;
  if (table.size() != count) {
    throw std::invalid_argument("MsnzbGate::Apply: table must have " +
                                std::to_string(count) + " entries");
  }
  const std::size_t n = S.size();
  SharedVec<Ring> out(n);

  for (std::size_t j = 0; j < n; ++j) {
    // ---- open c = S + r. ONE round for the whole batch; charged below. ----
    const Ring opened = static_cast<Ring>(S.p[0][j].lo + S.p[1][j].lo +
                                          S.p[2][j].lo);
    const u128 c = static_cast<u128>(static_cast<Sg>(opened)) + r_;

    // ---- evaluate every threshold at that one point ----------------------
    // 1[S >= 2^k] = 1 - 1[S < 2^k], and the DCF gives the second directly.
    // Telescoping: value = table[0] + sum_k (table[k] - table[k-1]) * ge_k.
    Ring acc0 = table[0], acc1 = 0;
    for (std::size_t idx = 1; idx < count; ++idx) {
      const Ring step = static_cast<Ring>(table[idx] - table[idx - 1]);
      // ge = 1 - lt, so shares of ge are (1 - lt_0) and (-lt_1) under the
      // difference convention: (1 - lt0) - (-lt1) = 1 - (lt0 - lt1).
      const Ring lt0 = EvalDcf<Ring>(k0_[idx], c);
      const Ring lt1 = EvalDcf<Ring>(k1_[idx], c);
      acc0 = static_cast<Ring>(acc0 + step * static_cast<Ring>(Ring(1) - lt0));
      acc1 = static_cast<Ring>(acc1 + step * static_cast<Ring>(Ring(0) - lt1));
    }

    // Assemble a replicated sharing of (acc0 - acc1). P0 and P1 carry the two
    // halves; P2 contributes nothing, and the re-randomisation that keeps it
    // from inverting the result comes from the pairwise secret.
    const Ring mask = s.Gen(0).NextPairwise();
    const Ring mask_chk = s.Gen(1).PrevPairwise();
    (void)mask_chk;
    s.Gen(2).Skip(1);          // keep all three counters in step

    Ring z[3];
    z[0] = static_cast<Ring>(acc0 + mask);
    z[1] = static_cast<Ring>(Ring(0) - acc1 - mask);
    z[2] = Ring(0);
    for (int i = 0; i < 3; ++i) {
      out.p[static_cast<std::size_t>(i)][j] =
          ReplicatedShare<Ring>(z[i], z[(i + 1) % 3]);
    }
  }

  // One round to open c, one to reshare. Every threshold rode along.
  s.AccountRound(3 * n);
  s.AccountRound(2 * n);
  return out;
}

// --------------------------------------------------------------------------
//  Inverse square root, shared. Same structure as InvSqrtClear, so the
//  cleartext calibration (4 Newton steps) carries over directly.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> InvSqrtShared(Mpc3<Ring>& s, const SharedVec<Ring>& S,
                              std::uint32_t t, int newton_steps,
                              const MsnzbGate<Ring>& gate) {
  const std::size_t n = S.size();

  // The seed table: if msnzb(S) == k then S ~ 2^(k-t) and 1/sqrt(S) ~
  // 2^(-(k-t)/2), which at scale t is 2^((3t-k)/2).
  std::vector<Ring> table;
  for (std::uint32_t k = gate.Lo(); k <= gate.Hi(); ++k) {
    const int shift = (3 * static_cast<int>(t) - static_cast<int>(k)) / 2;
    table.push_back((shift >= 0 && shift < RingTraits<Ring>::kBits)
                        ? static_cast<Ring>(Ring(1) << shift)
                        : Ring(1));
  }

  SharedVec<Ring> y = gate.Apply(s, S, table);

  // y <- y (3 - S y^2) / 2, exactly as in the cleartext reference.
  const Ring three = static_cast<Ring>(Ring(3) << t);
  for (int step = 0; step < newton_steps; ++step) {
    auto sy = TruncatePair<Ring>(s, s.MulVec(S, y), t);
    auto sy2 = TruncatePair<Ring>(s, s.MulVec(sy, y), t);

    // 3 - S y^2 : subtracting a share from a public constant is local, and
    // the public part is added by ONE party only or it would be doubled.
    SharedVec<Ring> corr(n);
    for (int i = 0; i < 3; ++i) {
      for (std::size_t j = 0; j < n; ++j) {
        const auto& v = sy2.p[static_cast<std::size_t>(i)][j];
        corr.p[static_cast<std::size_t>(i)][j] =
            ReplicatedShare<Ring>(static_cast<Ring>((i == 0 ? three : Ring(0)) - v.lo),
                                  static_cast<Ring>((i == 2 ? three : Ring(0)) - v.hi));
      }
    }

    // y * corr is at scale 2t, and the Newton step wants it halved. Truncating
    // by t+1 rather than t does BOTH in one protocol call.
    //
    // Halving the shares locally instead would be wrong for exactly the reason
    // TruncateLocal is wrong: the shares are uniform over the ring, their sum
    // wraps, and shifting each one independently loses the carry. That mistake
    // would look like slow Newton convergence rather than like a bug.
    y = TruncatePair<Ring>(s, s.MulVec(y, corr), t + 1);
  }
  return y;
}

template class MsnzbGate<u64>;
template class MsnzbGate<u128>;
template SharedVec<u64> InvSqrtShared<u64>(Mpc3<u64>&, const SharedVec<u64>&,
                                           std::uint32_t, int,
                                           const MsnzbGate<u64>&);
template SharedVec<u128> InvSqrtShared<u128>(Mpc3<u128>&, const SharedVec<u128>&,
                                             std::uint32_t, int,
                                             const MsnzbGate<u128>&);

}  // namespace oblivrec
