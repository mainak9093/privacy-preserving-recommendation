// ==========================================================================
//  mpc.cpp -- the 3PC substrate. See mpc.hpp for the protocol and its cost.
// ==========================================================================
#include "oblivrec/mpc.hpp"

#include <cstring>
#include <memory>

#include "oblivrec/csprng.hpp"

namespace oblivrec {
namespace {

// Index arithmetic around the ring of three, written once so an off-by-one
// cannot hide in four different loops.
inline int Next(int i) { return (i + 1) % 3; }
inline int Prev(int i) { return (i + 2) % 3; }

}  // namespace

// --------------------------------------------------------------------------
//  Correlated randomness.
// --------------------------------------------------------------------------
template <typename Ring>
Ring ZeroShareGen<Ring>::Eval(const AesKeySchedule& k, std::uint64_t ctr) {
  // AES in counter mode gives 16 pseudorandom bytes; take as many as the ring
  // needs, little-endian, matching RingTraits::FromBytes everywhere else.
  alignas(16) std::uint8_t in[16] = {0};
  for (int i = 0; i < 8; ++i) in[i] = static_cast<std::uint8_t>(ctr >> (8 * i));
  __m128i out = k.Encrypt(_mm_load_si128(reinterpret_cast<__m128i*>(in)));
  alignas(16) std::uint8_t buf[16];
  _mm_store_si128(reinterpret_cast<__m128i*>(buf), out);
  return RingTraits<Ring>::FromBytes(buf);
}

template <typename Ring>
Ring ZeroShareGen<Ring>::Next() {
  // alpha_i = F(k_i, ctr) - F(k_{i-1}, ctr). Party i's "next" key is party
  // i+1's "prev" key, so the two evaluations cancel around the ring.
  const Ring a = Eval(next_, ctr_);
  const Ring b = Eval(prev_, ctr_);
  ++ctr_;
  return static_cast<Ring>(a - b);
}

template <typename Ring>
Ring ZeroShareGen<Ring>::NextPairwise() {
  const Ring a = Eval(next_, ctr_);
  ++ctr_;
  return a;
}

template <typename Ring>
Ring ZeroShareGen<Ring>::PrevPairwise() {
  // The neighbour's NextPairwise() evaluates the same key at the same counter,
  // so the pair agrees and the third party -- which holds neither copy of this
  // key -- cannot predict it.
  const Ring a = Eval(prev_, ctr_);
  ++ctr_;
  return a;
}

// --------------------------------------------------------------------------
//  Session setup.
// --------------------------------------------------------------------------
template <typename Ring>
Mpc3<Ring>::Mpc3(std::uint64_t seed) {
  // Three pairwise keys: key[i] is shared between party i and party i+1.
  // Derived from the seed so a failing test reproduces exactly; the real
  // deployment would exchange these once at startup over the authenticated
  // channel, which is why they are keys rather than a shared counter.
  std::uint8_t key[3][16];
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 16; ++j) {
      key[i][j] = static_cast<std::uint8_t>((seed >> ((j % 8) * 8)) + i * 31 + j * 7);
    }
  }
  for (int i = 0; i < 3; ++i) {
    // Party i holds the key it shares with i+1, and the one it shares with
    // i-1 -- which is key[Prev(i)], since key[j] belongs to the pair (j, j+1).
    gens_[static_cast<std::size_t>(i)].reset(
        new ZeroShareGen<Ring>(key[i], key[Prev(i)]));
    priv_state_[static_cast<std::size_t>(i)] = seed * 1000003ull + i * 7919ull;
  }
}

template <typename Ring>
Ring Mpc3<Ring>::PrivateRandom(int party) {
  // splitmix64, stepped per party. Deterministic on purpose: this stands in
  // for the OS CSPRNG so that a protocol failure is reproducible.
  std::uint64_t& st = priv_state_[static_cast<std::size_t>(party)];
  st += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = st;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z = z ^ (z >> 31);
  Ring r = static_cast<Ring>(z);
  if (RingTraits<Ring>::kBytes > 8) {
    st += 0x9E3779B97F4A7C15ull;
    std::uint64_t w = st;
    w = (w ^ (w >> 30)) * 0xBF58476D1CE4E5B9ull;
    w = (w ^ (w >> 27)) * 0x94D049BB133111EBull;
    w = w ^ (w >> 31);
    r = static_cast<Ring>((r << 64) ^ static_cast<Ring>(w));
  }
  return r;
}

// --------------------------------------------------------------------------
//  The exchange, and the accounting. Every communicating op goes through here.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> Mpc3<Ring>::Exchange(const std::array<std::vector<Ring>, 3>& z) {
  const std::size_t n = z[0].size();
  SharedVec<Ring> out(n);
  for (int i = 0; i < 3; ++i) {
    // Party i ends up holding (z_i, z_{i+1}): its own local share, and the one
    // its neighbour sent. That is the replicated form, so results compose.
    for (std::size_t j = 0; j < n; ++j) {
      out.p[static_cast<std::size_t>(i)][j] =
          ReplicatedShare<Ring>(z[static_cast<std::size_t>(i)][j],
                                z[static_cast<std::size_t>(Next(i))][j]);
    }
  }
  ++rounds_;
  // One ring element per element of the result, per party. Three parties each
  // send n elements, so 3n in total -- this is what the Thm 4.2 test checks.
  bytes_ += static_cast<std::uint64_t>(3 * n) *
            static_cast<std::uint64_t>(RingTraits<Ring>::kBytes);
  return out;
}

// --------------------------------------------------------------------------
//  Degree-two operations.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> Mpc3<Ring>::MulVec(const SharedVec<Ring>& x,
                                   const SharedVec<Ring>& y) {
  if (!x.Consistent() || !y.Consistent() || x.size() != y.size()) {
    throw std::invalid_argument("MulVec: operand shapes disagree");
  }
  const std::size_t n = x.size();
  std::array<std::vector<Ring>, 3> z;
  for (int i = 0; i < 3; ++i) {
    auto& zi = z[static_cast<std::size_t>(i)];
    zi.resize(n);
    auto& gen = Gen(i);
    for (std::size_t j = 0; j < n; ++j) {
      const auto& a = x.p[static_cast<std::size_t>(i)][j];
      const auto& b = y.p[static_cast<std::size_t>(i)][j];
      // a.lo = x_i, a.hi = x_{i+1}; likewise for b.
      zi[j] = static_cast<Ring>(a.lo * b.lo + a.lo * b.hi + a.hi * b.lo +
                                gen.Next());
    }
  }
  return Exchange(z);
}

template <typename Ring>
SharedVec<Ring> Mpc3<Ring>::InnerProduct(const SharedVec<Ring>& x,
                                         const SharedVec<Ring>& y) {
  if (!x.Consistent() || !y.Consistent() || x.size() != y.size()) {
    throw std::invalid_argument("InnerProduct: operand shapes disagree");
  }
  const std::size_t n = x.size();
  std::array<std::vector<Ring>, 3> z;
  for (int i = 0; i < 3; ++i) {
    auto& gen = Gen(i);
    Ring acc = 0;
    for (std::size_t j = 0; j < n; ++j) {
      const auto& a = x.p[static_cast<std::size_t>(i)][j];
      const auto& b = y.p[static_cast<std::size_t>(i)][j];
      // THE SUMMATION HAPPENS HERE, BEFORE THE EXCHANGE. That is the whole
      // trick: n products collapse to one element of communication.
      acc = static_cast<Ring>(acc + a.lo * b.lo + a.lo * b.hi + a.hi * b.lo);
    }
    z[static_cast<std::size_t>(i)].assign(1, static_cast<Ring>(acc + gen.Next()));
  }
  return Exchange(z);
}

template <typename Ring>
SharedVec<Ring> Mpc3<Ring>::MatVec(const SharedMatrix<Ring>& M,
                                   const SharedVec<Ring>& v) {
  if (M.data.size() != std::size_t(M.rows) * M.cols || v.size() != M.cols) {
    throw std::invalid_argument("MatVec: shapes disagree");
  }
  std::array<std::vector<Ring>, 3> z;
  for (int i = 0; i < 3; ++i) {
    auto& zi = z[static_cast<std::size_t>(i)];
    zi.assign(M.rows, Ring(0));
    auto& gen = Gen(i);
    for (std::uint32_t r = 0; r < M.rows; ++r) {
      Ring acc = 0;
      for (std::uint32_t c = 0; c < M.cols; ++c) {
        const auto& a = M.data.p[static_cast<std::size_t>(i)]
                                [std::size_t(r) * M.cols + c];
        const auto& b = v.p[static_cast<std::size_t>(i)][c];
        acc = static_cast<Ring>(acc + a.lo * b.lo + a.lo * b.hi + a.hi * b.lo);
      }
      zi[r] = static_cast<Ring>(acc + gen.Next());
    }
  }
  // `rows` elements per party, independent of `cols`. NUDGE Thm 4.2.
  return Exchange(z);
}

// --------------------------------------------------------------------------
//  Free operations. No exchange, so no round and no bytes.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> Mpc3<Ring>::MatVecPublic(Span<const Ring> M, std::uint32_t rows,
                                         std::uint32_t cols,
                                         const SharedVec<Ring>& v) {
  if (M.size() != std::size_t(rows) * cols || v.size() != cols) {
    throw std::invalid_argument("MatVecPublic: shapes disagree");
  }
  SharedVec<Ring> out(rows);
  for (int i = 0; i < 3; ++i) {
    for (std::uint32_t r = 0; r < rows; ++r) {
      ReplicatedShare<Ring> acc;
      for (std::uint32_t c = 0; c < cols; ++c) {
        acc = acc + v.p[static_cast<std::size_t>(i)][c].MulPublic(
                        M[std::size_t(r) * cols + c]);
      }
      out.p[static_cast<std::size_t>(i)][r] = acc;
    }
  }
  return out;
}

template <typename Ring>
SharedVec<Ring> Mpc3<Ring>::InnerProductPublic(const SharedVec<Ring>& x,
                                               Span<const Ring> pub) {
  if (x.size() != pub.size()) {
    throw std::invalid_argument("InnerProductPublic: shapes disagree");
  }
  SharedVec<Ring> out(1);
  for (int i = 0; i < 3; ++i) {
    ReplicatedShare<Ring> acc;
    for (std::size_t j = 0; j < x.size(); ++j) {
      acc = acc + x.p[static_cast<std::size_t>(i)][j].MulPublic(pub[j]);
    }
    out.p[static_cast<std::size_t>(i)][0] = acc;
  }
  return out;
}

// --------------------------------------------------------------------------
//  Split / open.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> SplitVec(Span<const Ring> values) {
  SharedVec<Ring> out(values.size());
  for (std::size_t j = 0; j < values.size(); ++j) {
    auto parts = Split<Ring>(values[j]);
    for (int i = 0; i < 3; ++i) {
      out.p[static_cast<std::size_t>(i)][j] = parts[static_cast<std::size_t>(i)];
    }
  }
  return out;
}

template <typename Ring>
std::vector<Ring> OpenVec(const SharedVec<Ring>& v) {
  std::vector<Ring> out(v.size());
  for (std::size_t j = 0; j < v.size(); ++j) {
    out[j] = static_cast<Ring>(v.p[0][j].lo + v.p[1][j].lo + v.p[2][j].lo);
  }
  return out;
}

// --------------------------------------------------------------------------
//  Explicit instantiation, matching every other .cpp in this project.
// --------------------------------------------------------------------------
template class ZeroShareGen<u64>;
template class ZeroShareGen<u128>;
template class Mpc3<u64>;
template class Mpc3<u128>;
template SharedVec<u64> SplitVec<u64>(Span<const u64>);
template SharedVec<u128> SplitVec<u128>(Span<const u128>);
template std::vector<u64> OpenVec<u64>(const SharedVec<u64>&);
template std::vector<u128> OpenVec<u128>(const SharedVec<u128>&);

}  // namespace oblivrec
