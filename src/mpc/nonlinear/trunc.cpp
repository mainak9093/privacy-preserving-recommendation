// ==========================================================================
//  trunc.cpp -- Trunc_t, both variants. See nonlinear.hpp for why there are
//  two and what the comparison between them is for.
// ==========================================================================
#include "oblivrec/nonlinear.hpp"

#include <stdexcept>

namespace oblivrec {
namespace {

inline int NextP(int i) { return (i + 1) % 3; }
inline int PrevP(int i) { return (i + 2) % 3; }

}  // namespace

// --------------------------------------------------------------------------
//  The cleartext oracle. ARITHMETIC shift: ring elements are two's complement,
//  so a logical shift would turn every negative value into a huge positive
//  one. Going through the signed twin is what makes the sign survive.
// --------------------------------------------------------------------------
template <typename Ring>
Ring TruncateClear(Ring x, std::uint32_t t) {
  using S = typename RingTraits<Ring>::Signed;
  return static_cast<Ring>(static_cast<S>(x) >> t);
}

// --------------------------------------------------------------------------
//  Variant 1: local. Zero rounds, and wrong when the shares wrap.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> TruncateLocal(const SharedVec<Ring>& x, std::uint32_t t) {
  using S = typename RingTraits<Ring>::Signed;
  const std::size_t n = x.size();
  SharedVec<Ring> out(n);
  for (int i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      const auto& sh = x.p[static_cast<std::size_t>(i)][j];
      // Both components shift, so party i's hi still equals party i+1's lo and
      // the result is a well-formed replicated sharing -- of the WRONG value
      // whenever x0+x1+x2 wrapped.
      out.p[static_cast<std::size_t>(i)][j] = ReplicatedShare<Ring>(
          static_cast<Ring>(static_cast<S>(sh.lo) >> t),
          static_cast<Ring>(static_cast<S>(sh.hi) >> t));
    }
  }
  return out;
}

// --------------------------------------------------------------------------
//  Variant 2: the correct one, via a helper-supplied pair (r, r >> t).
//
//  Roles: `h` is the helper. The other two are a = h+1 and b = h+2, which are
//  adjacent, so they share a PRF key that h does not hold -- that key is what
//  re-randomises the resharing at the end and stops h inverting it.
//
//  Three rounds, matching ARCHITECTURE section 4.1's stated cost:
//    (1) h distributes the pair             4 elements   [preprocessable]
//    (2) a and b open c = x - r             2 elements
//    (3) a and b reshare into replicated    2 elements
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> TruncatePair(Mpc3<Ring>& s, const SharedVec<Ring>& x,
                             std::uint32_t t, int helper) {
  if (helper < 0 || helper > 2) {
    throw std::invalid_argument("TruncatePair: helper must be 0, 1 or 2");
  }
  const int h = helper, a = NextP(h), b = PrevP(h);
  const std::size_t n = x.size();
  SharedVec<Ring> out(n);

  std::vector<Ring> za(n), zb(n);

  for (std::size_t j = 0; j < n; ++j) {
    // ---- (1) the helper builds the correlated pair ----------------------
    const Ring r = s.PrivateRandom(h);
    const Ring r_trunc = TruncateClear<Ring>(r, t);
    const Ring r_a = s.PrivateRandom(h);
    const Ring r_b = static_cast<Ring>(r - r_a);
    const Ring rt_a = s.PrivateRandom(h);
    const Ring rt_b = static_cast<Ring>(r_trunc - rt_a);

    // ---- (2) a and b open c = x - r -------------------------------------
    // A 2-of-2 split of x that h cannot assemble: a contributes the two
    // components it holds, b contributes the remaining one.
    //   party a holds (x_a, x_b);  party b holds (x_b, x_h)
    const Ring x_a = x.p[static_cast<std::size_t>(a)][j].lo;
    const Ring x_b = x.p[static_cast<std::size_t>(a)][j].hi;   // == party b's lo
    const Ring x_h = x.p[static_cast<std::size_t>(b)][j].hi;   // == party h's lo

    const Ring u_a = static_cast<Ring>(x_a + x_b - r_a);
    const Ring u_b = static_cast<Ring>(x_h - r_b);
    const Ring c = static_cast<Ring>(u_a + u_b);               // = x - r

    // Truncating a value both of them hold in the clear is free and exact.
    const Ring c_trunc = TruncateClear<Ring>(c, t);

    // y = (x - r)>>t + r>>t, which is x>>t up to one unit from the borrow.
    // Only one of the two adds the public part, or it would be counted twice.
    Ring y_a = static_cast<Ring>(c_trunc + rt_a);
    Ring y_b = rt_b;

    // ---- (3) reshare into 3-party replicated ----------------------------
    // Re-randomise with the a-b pairwise secret FIRST. Without this, h learns
    // y_a, already knows rt_a, and recovers c -- and with r that is x. This
    // one line is the difference between a protocol and a leak.
    const Ring mask = s.Gen(a).NextPairwise();
    const Ring mask_b = s.Gen(b).PrevPairwise();
    (void)mask_b;   // same value by construction; drawn so both sides agree
    // AND THE HELPER MUST ADVANCE TOO. a and b each stepped their counter; if
    // h does not, the three generators fall out of step and every LATER
    // zero-share stops summing to zero -- which silently corrupts every
    // subsequent multiplication rather than failing here. This cost a broken
    // factorisation to find, and Mpc3::Exchange now asserts the invariant.
    s.Gen(h).Skip(1);
    y_a = static_cast<Ring>(y_a + mask);
    y_b = static_cast<Ring>(y_b - mask);

    za[j] = y_a;
    zb[j] = y_b;
  }

  // Additive shares by party index: h contributes nothing.
  for (std::size_t j = 0; j < n; ++j) {
    Ring z[3];
    z[static_cast<std::size_t>(h)] = Ring(0);
    z[static_cast<std::size_t>(a)] = za[j];
    z[static_cast<std::size_t>(b)] = zb[j];
    for (int i = 0; i < 3; ++i) {
      out.p[static_cast<std::size_t>(i)][j] = ReplicatedShare<Ring>(
          z[static_cast<std::size_t>(i)], z[static_cast<std::size_t>(NextP(i))]);
    }
  }

  // Accounting, charged once for the whole batch: every element above rides in
  // the same message, which is why truncating a vector costs the same rounds
  // as truncating a scalar.
  s.AccountRound(4 * n);   // (1) helper distributes the pair
  s.AccountRound(2 * n);   // (2) open c
  s.AccountRound(2 * n);   // (3) reshare
  return out;
}


template class TruncStage<u64>;
template class TruncStage<u128>;

template u64 TruncateClear<u64>(u64, std::uint32_t);
template u128 TruncateClear<u128>(u128, std::uint32_t);
template SharedVec<u64> TruncateLocal<u64>(const SharedVec<u64>&, std::uint32_t);
template SharedVec<u128> TruncateLocal<u128>(const SharedVec<u128>&, std::uint32_t);
template SharedVec<u64> TruncatePair<u64>(Mpc3<u64>&, const SharedVec<u64>&,
                                          std::uint32_t, int);
template SharedVec<u128> TruncatePair<u128>(Mpc3<u128>&, const SharedVec<u128>&,
                                            std::uint32_t, int);

}  // namespace oblivrec
