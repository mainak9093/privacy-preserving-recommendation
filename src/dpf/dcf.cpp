// ==========================================================================
//  dcf.cpp -- the distributed comparison function. See dcf.hpp for why this
//  is not the DPF with a different payload.
//
//  Boyle, Chandran, Gilboa, Gupta, Ishai, Kumar, Rathee, "Function Secret
//  Sharing for Mixed-Mode and Fixed-Point Secure Computation", Fig. 1.
//
//  THE PART THAT IS EASY TO GET WRONG. At each level, one child is on alpha's
//  path (Keep) and one is not (Lose). If alpha's bit is 1 then alpha went
//  RIGHT, so the entire LEFT subtree is strictly below alpha and must carry
//  the payload. That is why beta is added when Lose == L, not when Keep == L.
//  Getting it backwards produces a function that is correct at exactly one
//  point and wrong everywhere else -- which looks like a payload bug rather
//  than a direction bug.
//
//  V_alpha is a running accumulator of everything already emitted below the
//  current level, so each level's correction word can subtract it back out.
//  It is the reason Gen cannot be written as an independent per-level loop.
// ==========================================================================
#include "oblivrec/dcf.hpp"

#include <cstring>
#include <stdexcept>

#include "oblivrec/csprng.hpp"

namespace oblivrec {
namespace {

// A ring element from a PRG block. Same convention as the DPF's Convert.
template <typename Ring>
inline Ring Convert(const Block& b) {
  std::uint8_t buf[16];
  b.ToBytes(buf);
  return RingTraits<Ring>::FromBytes(buf);
}

inline std::uint8_t BitAt(u128 x, std::uint32_t bits, std::uint32_t i) {
  // Most significant bit first, matching the DPF's traversal order so the two
  // primitives index the domain the same way.
  return static_cast<std::uint8_t>((x >> (bits - 1 - i)) & 1u);
}

}  // namespace

// --------------------------------------------------------------------------
//  Gen
// --------------------------------------------------------------------------
template <typename Ring>
std::pair<DcfKey<Ring>, DcfKey<Ring>> GenDcf(std::uint32_t domain_bits,
                                             u128 alpha, Ring beta) {
  if (domain_bits == 0 || domain_bits > 127) {
    throw std::invalid_argument("GenDcf: domain_bits must be in 1..127");
  }
  if (domain_bits < 128 && alpha >= (u128(1) << domain_bits)) {
    throw std::invalid_argument("GenDcf: alpha is outside the domain");
  }

  DcfKey<Ring> k0, k1;
  k0.party = 0; k1.party = 1;
  k0.domain_bits = k1.domain_bits = domain_bits;
  k0.cw.resize(domain_bits);
  k1.cw.resize(domain_bits);

  std::uint8_t sd[32];
  RandomBytes(sd, sizeof(sd));
  k0.seed = Block::FromBytes(sd);
  k1.seed = Block::FromBytes(sd + 16);

  Block s0 = k0.seed, s1 = k1.seed;
  std::uint8_t t0 = 0, t1 = 1;
  Ring v_alpha = 0;

  const FixedKeyPrg& prg = Prg();

  for (std::uint32_t i = 0; i < domain_bits; ++i) {
    Block e0[4], e1[4];
    prg.Expand4(s0, e0);
    prg.Expand4(s1, e1);
    // Layout: [0]=sL, [1]=vL, [2]=sR, [3]=vR.
    const Block& s0L = e0[0]; const Block& v0L = e0[1];
    const Block& s0R = e0[2]; const Block& v0R = e0[3];
    const Block& s1L = e1[0]; const Block& v1L = e1[1];
    const Block& s1R = e1[2]; const Block& v1R = e1[3];
    const std::uint8_t t0L = s0L.Lsb(), t0R = s0R.Lsb();
    const std::uint8_t t1L = s1L.Lsb(), t1R = s1R.Lsb();

    const std::uint8_t a = BitAt(alpha, domain_bits, i);
    const bool keep_left = (a == 0);

    // Lose is the child OFF alpha's path.
    const Block& s0Lose = keep_left ? s0R : s0L;
    const Block& s1Lose = keep_left ? s1R : s1L;
    const Block& v0Lose = keep_left ? v0R : v0L;
    const Block& v1Lose = keep_left ? v1R : v1L;
    const Block& s0Keep = keep_left ? s0L : s0R;
    const Block& s1Keep = keep_left ? s1L : s1R;
    const Block& v0Keep = keep_left ? v0L : v0R;
    const Block& v1Keep = keep_left ? v1L : v1R;

    const Block s_cw = s0Lose ^ s1Lose;

    // The sign that makes the two parties' corrections cancel.
    const bool neg = (t1 != 0);
    auto Signed = [neg](Ring x) -> Ring {
      return neg ? static_cast<Ring>(Ring(0) - x) : x;
    };

    Ring v_cw = Signed(static_cast<Ring>(Convert<Ring>(v1Lose) -
                                         Convert<Ring>(v0Lose) - v_alpha));
    // THE DIRECTION. Lose == L exactly when alpha's bit is 1, i.e. alpha went
    // right and the whole left subtree is below it. That subtree gets beta.
    if (!keep_left) {
      v_cw = static_cast<Ring>(v_cw + Signed(beta));
    }

    v_alpha = static_cast<Ring>(v_alpha - Convert<Ring>(v1Keep) +
                                Convert<Ring>(v0Keep) + Signed(v_cw));

    const std::uint8_t tL_cw =
        static_cast<std::uint8_t>(t0L ^ t1L ^ a ^ 1u);
    const std::uint8_t tR_cw = static_cast<std::uint8_t>(t0R ^ t1R ^ a);

    for (auto* k : {&k0, &k1}) {
      k->cw[i].s = s_cw;
      k->cw[i].v = v_cw;
      k->cw[i].tL = tL_cw;
      k->cw[i].tR = tR_cw;
    }

    const std::uint8_t tKeep_cw = keep_left ? tL_cw : tR_cw;
    const Block s0next = t0 ? (s0Keep ^ s_cw) : s0Keep;
    const Block s1next = t1 ? (s1Keep ^ s_cw) : s1Keep;
    const std::uint8_t t0Keep = keep_left ? t0L : t0R;
    const std::uint8_t t1Keep = keep_left ? t1L : t1R;
    const std::uint8_t t0next =
        static_cast<std::uint8_t>(t0Keep ^ (t0 ? tKeep_cw : 0u));
    const std::uint8_t t1next =
        static_cast<std::uint8_t>(t1Keep ^ (t1 ? tKeep_cw : 0u));

    s0 = s0next; s1 = s1next; t0 = t0next; t1 = t1next;
  }

  const bool neg = (t1 != 0);
  Ring last = static_cast<Ring>(Convert<Ring>(s1) - Convert<Ring>(s0) - v_alpha);
  if (neg) last = static_cast<Ring>(Ring(0) - last);
  k0.cw_last = last;
  k1.cw_last = last;
  return {k0, k1};
}

// --------------------------------------------------------------------------
//  Eval
// --------------------------------------------------------------------------
template <typename Ring>
Ring EvalDcf(const DcfKey<Ring>& key, u128 x) {
  if (key.party > 1) {
    throw std::invalid_argument("EvalDcf: party byte must be 0 or 1");
  }
  if (key.cw.size() != key.domain_bits) {
    throw std::invalid_argument("EvalDcf: key has the wrong number of levels");
  }

  Block s = key.seed;
  std::uint8_t t = key.party;
  Ring acc = 0;
  const FixedKeyPrg& prg = Prg();

  for (std::uint32_t i = 0; i < key.domain_bits; ++i) {
    Block e[4];
    prg.Expand4(s, e);
    const Block& sL = e[0]; const Block& vL = e[1];
    const Block& sR = e[2]; const Block& vR = e[3];
    const std::uint8_t tL = sL.Lsb(), tR = sR.Lsb();

    const std::uint8_t b = BitAt(x, key.domain_bits, i);
    const Block& v_here = b ? vR : vL;
    const Block& s_here = b ? sR : sL;
    const std::uint8_t t_here = b ? tR : tL;
    const std::uint8_t t_cw = b ? key.cw[i].tR : key.cw[i].tL;

    Ring term = static_cast<Ring>(Convert<Ring>(v_here) +
                                  (t ? key.cw[i].v : Ring(0)));
    acc = key.party ? static_cast<Ring>(acc - term)
                    : static_cast<Ring>(acc + term);

    s = t ? (s_here ^ key.cw[i].s) : s_here;
    t = static_cast<std::uint8_t>(t_here ^ (t ? t_cw : 0u));
  }

  Ring term = static_cast<Ring>(Convert<Ring>(s) + (t ? key.cw_last : Ring(0)));
  acc = key.party ? static_cast<Ring>(acc - term)
                  : static_cast<Ring>(acc + term);

  // SUM convention internally (see dcf.hpp). Negating party 1 converts to the
  // DIFFERENCE convention the rest of the project uses, so a caller never has
  // to remember which primitive it is holding.
  return key.party ? static_cast<Ring>(Ring(0) - acc) : acc;
}

template <typename Ring>
void EvalFullDcf(const DcfKey<Ring>& key, Span<Ring> out) {
  const std::uint64_t n = std::uint64_t(1) << key.domain_bits;
  if (out.size() != n) {
    throw std::invalid_argument("EvalFullDcf: out must be 2^domain_bits");
  }
  // A flat loop rather than a shared tree walk. The DCF's accumulator makes a
  // single-pass tree traversal awkward, and every consumer here evaluates over
  // small domains (the MSNZB gate uses domain_bits <= 7), so the simple
  // version is the right one until a profile says otherwise.
  for (std::uint64_t x = 0; x < n; ++x) {
    out[static_cast<std::size_t>(x)] = EvalDcf<Ring>(key, x);
  }
}

// --------------------------------------------------------------------------
//  Serialisation
// --------------------------------------------------------------------------
template <typename Ring>
std::size_t DcfKey<Ring>::SizeBytes() const {
  return 1 + 4 + 16 +
         std::size_t(domain_bits) * (16 + std::size_t(RingTraits<Ring>::kBytes) + 2) +
         std::size_t(RingTraits<Ring>::kBytes);
}

template <typename Ring>
std::vector<std::uint8_t> DcfKey<Ring>::Serialize() const {
  std::vector<std::uint8_t> out;
  out.reserve(SizeBytes());
  out.push_back(party);
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>(domain_bits >> (8 * i)));
  }
  std::uint8_t b[16];
  seed.ToBytes(b);
  out.insert(out.end(), b, b + 16);
  for (const auto& c : cw) {
    c.s.ToBytes(b);
    out.insert(out.end(), b, b + 16);
    const std::size_t at = out.size();
    out.resize(at + RingTraits<Ring>::kBytes);
    RingTraits<Ring>::ToBytes(c.v, out.data() + at);
    out.push_back(c.tL);
    out.push_back(c.tR);
  }
  const std::size_t at = out.size();
  out.resize(at + RingTraits<Ring>::kBytes);
  RingTraits<Ring>::ToBytes(cw_last, out.data() + at);
  return out;
}

template <typename Ring>
DcfKey<Ring> DcfKey<Ring>::Deserialize(Span<const std::uint8_t> in) {
  // Validate before use, same discipline as DpfKey::Deserialize -- keys arrive
  // over a wire and a party byte of 7 must not silently evaluate as party 1.
  if (in.size() < 21) throw std::invalid_argument("DcfKey: input too short");
  DcfKey<Ring> k;
  k.party = in[0];
  if (k.party > 1) throw std::invalid_argument("DcfKey: party byte not 0 or 1");
  k.domain_bits = 0;
  for (int i = 0; i < 4; ++i) {
    k.domain_bits |= static_cast<std::uint32_t>(in[1 + i]) << (8 * i);
  }
  if (k.domain_bits == 0 || k.domain_bits > 63) {
    throw std::invalid_argument("DcfKey: domain_bits out of range");
  }
  const std::size_t w = RingTraits<Ring>::kBytes;
  const std::size_t need = 1 + 4 + 16 + std::size_t(k.domain_bits) * (16 + w + 2) + w;
  if (in.size() != need) {
    throw std::invalid_argument("DcfKey: length " + std::to_string(in.size()) +
                                " does not match domain_bits " +
                                std::to_string(k.domain_bits));
  }
  std::size_t at = 5;
  std::uint8_t b[16];
  std::memcpy(b, &in[at], 16);
  k.seed = Block::FromBytes(b);
  at += 16;
  k.cw.resize(k.domain_bits);
  for (auto& c : k.cw) {
    std::memcpy(b, &in[at], 16);
    c.s = Block::FromBytes(b);
    at += 16;
    c.v = RingTraits<Ring>::FromBytes(&in[at]);
    at += w;
    c.tL = in[at++];
    c.tR = in[at++];
  }
  k.cw_last = RingTraits<Ring>::FromBytes(&in[at]);
  return k;
}

template struct DcfKey<u64>;
template struct DcfKey<u128>;
template std::pair<DcfKey<u64>, DcfKey<u64>> GenDcf<u64>(std::uint32_t, u128,
                                                         u64);
template std::pair<DcfKey<u128>, DcfKey<u128>> GenDcf<u128>(std::uint32_t, u128,
                                                            u128);
template u64 EvalDcf<u64>(const DcfKey<u64>&, u128);
template u128 EvalDcf<u128>(const DcfKey<u128>&, u128);
template void EvalFullDcf<u64>(const DcfKey<u64>&, Span<u64>);
template void EvalFullDcf<u128>(const DcfKey<u128>&, Span<u128>);

}  // namespace oblivrec
