// ==========================================================================
//  share.hpp -- 2-out-of-3 replicated secret sharing over the ring.
//
//  ARCHITECTURE section 3.1 fixes the scheme: x = x0 + x1 + x2 over
//  Z_{2^b}, and party i holds the PAIR (x_i, x_{i+1}) with indices mod 3.
//  Any two parties together hold all three summands and can reconstruct.
//  Any one party holds two of three and learns nothing.
//
//  ------------------------------------------------------------------------
//  WHY THIS EXISTS NOW, when S1 only needs linear operations.
//
//  S1's serving path is scores = a . B with B public, which is a LOCAL
//  linear map on shares and needs no interaction at all. That could have
//  been done with plain additive sharing in half the code.
//
//  It is written as replicated sharing anyway because it is the exact type
//  S2 needs for the non-interactive matrix-vector product, and because
//  swapping the share type underneath a working serving path in October is
//  a worse job than writing sixty lines now. No protocol is attached: there
//  is no PRF setup, no re-randomisation, no multiplication. Those arrive
//  with S2 and this type is the thing they attach to.
//
//  ------------------------------------------------------------------------
//  WHAT IS DELIBERATELY ABSENT. Multiplication of two shared values needs
//  correlated randomness and a communication round (ARCHITECTURE 3.2), and
//  is S2 work. Only operations that are genuinely local live here, so that
//  nothing in S1 can accidentally depend on a protocol that does not exist
//  yet.
// ==========================================================================
#ifndef OBLIVREC_SHARE_HPP
#define OBLIVREC_SHARE_HPP

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "oblivrec/csprng.hpp"
#include "oblivrec/ring.hpp"

namespace oblivrec {

// Party i holds (lo, hi) = (x_i, x_{i+1}).
template <typename Ring>
struct ReplicatedShare {
  Ring lo = 0;
  Ring hi = 0;

  ReplicatedShare() = default;
  ReplicatedShare(Ring l, Ring h) : lo(l), hi(h) {}

  // Addition of two shared values is componentwise and local.
  ReplicatedShare operator+(const ReplicatedShare& o) const {
    return ReplicatedShare(static_cast<Ring>(lo + o.lo),
                           static_cast<Ring>(hi + o.hi));
  }
  ReplicatedShare operator-(const ReplicatedShare& o) const {
    return ReplicatedShare(static_cast<Ring>(lo - o.lo),
                           static_cast<Ring>(hi - o.hi));
  }
  // Multiplication by a PUBLIC scalar is componentwise and local. This is
  // the only multiplication S1 needs, because B is public.
  ReplicatedShare MulPublic(Ring c) const {
    return ReplicatedShare(static_cast<Ring>(lo * c), static_cast<Ring>(hi * c));
  }
};

// Split x into three shares, one per party, using the OS CSPRNG.
// Returns {P0, P1, P2} where Pi = (x_i, x_{i+1}).
template <typename Ring>
std::vector<ReplicatedShare<Ring>> Split(Ring x) {
  std::uint8_t buf[2 * sizeof(Ring)];
  RandomBytes(buf, sizeof(buf));
  const Ring x0 = RingTraits<Ring>::FromBytes(buf);
  const Ring x1 = RingTraits<Ring>::FromBytes(buf + RingTraits<Ring>::kBytes);
  const Ring x2 = static_cast<Ring>(x - x0 - x1);
  return {ReplicatedShare<Ring>(x0, x1),
          ReplicatedShare<Ring>(x1, x2),
          ReplicatedShare<Ring>(x2, x0)};
}

// Reconstruct from all three parties. Each summand appears twice across the
// three shares; we take each once, from its owning party.
template <typename Ring>
Ring Reconstruct(const std::vector<ReplicatedShare<Ring>>& parties) {
  if (parties.size() != 3) {
    throw std::invalid_argument("Reconstruct: expected 3 parties");
  }
  return static_cast<Ring>(parties[0].lo + parties[1].lo + parties[2].lo);
}

// Reconstruct from any TWO parties, which is what 2-of-3 means. Party pair
// (i, j) must be adjacent mod 3; the two together hold all three summands.
template <typename Ring>
Ring ReconstructPair(const ReplicatedShare<Ring>& pi,
                     const ReplicatedShare<Ring>& pj) {
  // pi = (x_i, x_{i+1}), pj = (x_{i+1}, x_{i+2}). Between them: x_i from
  // pi.lo, x_{i+1} from pi.hi, x_{i+2} from pj.hi.
  if (pi.hi != pj.lo) {
    throw std::invalid_argument(
        "ReconstructPair: shares are not from adjacent parties");
  }
  return static_cast<Ring>(pi.lo + pi.hi + pj.hi);
}

}  // namespace oblivrec
#endif  // OBLIVREC_SHARE_HPP
