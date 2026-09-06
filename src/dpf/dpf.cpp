// ==========================================================================
//  dpf.cpp -- Boyle-Gilboa-Ishai (2,2)-DPF.
//
//  Written by us, per REQUIREMENTS D2 and RULES C1. This is the
//  pedagogically load-bearing component and the thing a viva probes hardest,
//  so it is not a library import.
//
//  See dpf.hpp for the sign convention, which is the single most confusing
//  part of the construction.
// ==========================================================================
#include "oblivrec/dpf.hpp"

#include "oblivrec/csprng.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace oblivrec {
namespace detail {

// --------------------------------------------------------------------------
//  The one traversal step. Eval, EvalFull and the test checker all call this,
//  so a traversal bug has one home rather than three.
// --------------------------------------------------------------------------
void Traverse(Block* s, std::uint8_t* t, const CorrectionWord& cw, int dir) {
  Block sL, sR;
  Prg().Expand(*s, &sL, &sR);

  // Control bits are read BEFORE corrections are applied. The correction
  // word's own low bit will also flip the seed's low bit, but t is tracked
  // separately from here on and never re-derived from the seed, which is what
  // makes the 127-effective-bit convention safe.
  std::uint8_t tL = sL.Lsb();
  std::uint8_t tR = sR.Lsb();

  if (*t) {
    sL = sL ^ cw.s;
    sR = sR ^ cw.s;
    tL = static_cast<std::uint8_t>(tL ^ cw.tL);
    tR = static_cast<std::uint8_t>(tR ^ cw.tR);
  }

  if (dir == 0) { *s = sL; *t = tL; }
  else          { *s = sR; *t = tR; }
}

template <typename Ring>
Ring Convert(const Block& b) {
  std::uint8_t buf[16];
  b.ToBytes(buf);
  return RingTraits<Ring>::FromBytes(buf);
}

template u64  Convert<u64>(const Block&);
template u128 Convert<u128>(const Block&);

}  // namespace detail

namespace {

// Key material comes from the OS CSPRNG. See include/oblivrec/csprng.hpp for
// why this is not a std::mt19937_64: the DPF's initial seeds are the secret,
// and a Mersenne Twister is reconstructible from its own output.
Block RandomBlock() {
  std::uint8_t buf[16];
  RandomBytes(buf, sizeof(buf));
  return Block::FromBytes(buf);
}

}  // namespace

// --------------------------------------------------------------------------
//  Gen
// --------------------------------------------------------------------------
template <typename Ring>
std::pair<DpfKey<Ring>, DpfKey<Ring>> Gen(std::uint32_t alpha,
                                          Ring beta,
                                          std::uint32_t domain_bits) {
  if (domain_bits == 0 || domain_bits > 31)
    throw std::invalid_argument("domain_bits must be in 1..31");
  if (domain_bits < 32 && alpha >= (1u << domain_bits))
    throw std::invalid_argument("alpha outside the domain");

  DpfKey<Ring> k0, k1;
  k0.party = 0; k1.party = 1;
  k0.domain_bits = k1.domain_bits = domain_bits;
  k0.seed = RandomBlock();
  k1.seed = RandomBlock();
  k0.cw.resize(domain_bits);
  k1.cw.resize(domain_bits);

  Block s0 = k0.seed, s1 = k1.seed;
  std::uint8_t t0 = 0, t1 = 1;   // the root invariant: t0 XOR t1 == 1

  for (std::uint32_t i = 0; i < domain_bits; ++i) {
    // MSB-first: bit i of alpha counting from the top.
    const int bit =
        static_cast<int>((alpha >> (domain_bits - 1 - i)) & 1u);

    Block s0L, s0R, s1L, s1R;
    Prg().Expand(s0, &s0L, &s0R);
    Prg().Expand(s1, &s1L, &s1R);
    const std::uint8_t t0L = s0L.Lsb(), t0R = s0R.Lsb();
    const std::uint8_t t1L = s1L.Lsb(), t1R = s1R.Lsb();

    // "keep" is the direction alpha goes; "lose" is the sibling. The
    // correction is chosen to make the two parties' LOSE children identical,
    // so everything off the path cancels.
    const Block& s0_lose = bit ? s0L : s0R;
    const Block& s1_lose = bit ? s1L : s1R;

    CorrectionWord cw;
    cw.s  = s0_lose ^ s1_lose;
    cw.tL = static_cast<std::uint8_t>(t0L ^ t1L ^ bit ^ 1);
    cw.tR = static_cast<std::uint8_t>(t0R ^ t1R ^ bit);
    k0.cw[i] = cw;
    k1.cw[i] = cw;   // both parties hold the SAME correction words

    // Descend, applying the correction where the control bit says to.
    const Block&      s0_keep = bit ? s0R : s0L;
    const Block&      s1_keep = bit ? s1R : s1L;
    const std::uint8_t t0_keep = bit ? t0R : t0L;
    const std::uint8_t t1_keep = bit ? t1R : t1L;
    const std::uint8_t tcw_keep = bit ? cw.tR : cw.tL;

    s0 = t0 ? (s0_keep ^ cw.s) : s0_keep;
    s1 = t1 ? (s1_keep ^ cw.s) : s1_keep;
    t0 = static_cast<std::uint8_t>(t0_keep ^ (t0 & tcw_keep));
    t1 = static_cast<std::uint8_t>(t1_keep ^ (t1 & tcw_keep));
  }

  // Final ring correction. The (-1)^{t1} factor is what makes the DIFFERENCE
  // convention in dpf.hpp come out to beta rather than -beta.
  const Ring c0 = detail::Convert<Ring>(s0);
  const Ring c1 = detail::Convert<Ring>(s1);
  Ring last = static_cast<Ring>(beta - c0 + c1);
  if (t1) last = static_cast<Ring>(Ring(0) - last);
  k0.cw_last = last;
  k1.cw_last = last;

  return {k0, k1};
}

// --------------------------------------------------------------------------
//  Eval
// --------------------------------------------------------------------------
template <typename Ring>
Ring Eval(const DpfKey<Ring>& key, std::uint32_t x) {
  Block s = key.seed;
  std::uint8_t t = key.party;
  for (std::uint32_t i = 0; i < key.domain_bits; ++i) {
    const int dir =
        static_cast<int>((x >> (key.domain_bits - 1 - i)) & 1u);
    detail::Traverse(&s, &t, key.cw[i], dir);
  }
  Ring out = detail::Convert<Ring>(s);
  if (t) out = static_cast<Ring>(out + key.cw_last);
  return out;   // no (-1)^party factor: see the sign note in dpf.hpp
}

// --------------------------------------------------------------------------
//  EvalFull -- depth-first, O(domain_bits) working memory.
// --------------------------------------------------------------------------
namespace {

template <typename Ring>
void EvalFullRec(const DpfKey<Ring>& key, Block s, std::uint8_t t,
                 std::uint32_t level, std::uint32_t index, Span<Ring> out) {
  if (level == key.domain_bits) {
    Ring v = detail::Convert<Ring>(s);
    if (t) v = static_cast<Ring>(v + key.cw_last);
    out[index] = v;
    return;
  }
  for (int dir = 0; dir < 2; ++dir) {
    Block cs = s;
    std::uint8_t ct = t;
    detail::Traverse(&cs, &ct, key.cw[level], dir);
    EvalFullRec(key, cs, ct, level + 1, (index << 1) | static_cast<std::uint32_t>(dir), out);
  }
}

}  // namespace

template <typename Ring>
void EvalFull(const DpfKey<Ring>& key, Span<Ring> out) {
  // 2^24 leaves is already 128 MB at b=64. Anything larger is a design error
  // rather than a workload, so fail loudly instead of thrashing.
  assert(key.domain_bits <= 24 && "EvalFull domain too large; see comment");
  const std::size_t want = std::size_t(1) << key.domain_bits;
  if (out.size() != want)
    throw std::invalid_argument("EvalFull: out span must be 2^domain_bits");
  EvalFullRec<Ring>(key, key.seed, key.party, 0, 0, out);
}

// --------------------------------------------------------------------------
//  Serialisation. Packed, little-endian, self-describing.
//    [0]      party
//    [1..4]   domain_bits (u32 LE)
//    [5..20]  seed
//    then domain_bits * (16 seed + 1 tL + 1 tR)
//    then sizeof(Ring) for cw_last
// --------------------------------------------------------------------------
template <typename Ring>
std::size_t DpfKey<Ring>::SizeBytes() const {
  return 1 + 4 + 16 + std::size_t(domain_bits) * 18 + RingTraits<Ring>::kBytes;
}

template <typename Ring>
std::vector<std::uint8_t> DpfKey<Ring>::Serialize() const {
  std::vector<std::uint8_t> out(SizeBytes());
  std::size_t p = 0;
  out[p++] = party;
  for (int i = 0; i < 4; ++i)
    out[p++] = static_cast<std::uint8_t>((domain_bits >> (8 * i)) & 0xff);
  seed.ToBytes(&out[p]); p += 16;
  for (std::uint32_t i = 0; i < domain_bits; ++i) {
    cw[i].s.ToBytes(&out[p]); p += 16;
    out[p++] = cw[i].tL;
    out[p++] = cw[i].tR;
  }
  RingTraits<Ring>::ToBytes(cw_last, &out[p]);
  p += RingTraits<Ring>::kBytes;
  (void)p;
  return out;
}

template <typename Ring>
DpfKey<Ring> DpfKey<Ring>::Deserialize(Span<const std::uint8_t> in) {
  DpfKey<Ring> k;
  std::size_t p = 0;
  if (in.size() < 21) throw std::invalid_argument("DpfKey too short");
  k.party = in[p++];
  std::uint32_t db = 0;
  for (int i = 0; i < 4; ++i)
    db |= static_cast<std::uint32_t>(in[p++]) << (8 * i);
  k.domain_bits = db;
  std::uint8_t buf[16];
  for (int i = 0; i < 16; ++i) buf[i] = in[p++];
  k.seed = Block::FromBytes(buf);
  if (in.size() != k.SizeBytes())
    throw std::invalid_argument("DpfKey length mismatch");
  k.cw.resize(db);
  for (std::uint32_t i = 0; i < db; ++i) {
    for (int j = 0; j < 16; ++j) buf[j] = in[p++];
    k.cw[i].s  = Block::FromBytes(buf);
    k.cw[i].tL = in[p++];
    k.cw[i].tR = in[p++];
  }
  std::uint8_t rb[16] = {0};
  for (int i = 0; i < RingTraits<Ring>::kBytes; ++i) rb[i] = in[p++];
  k.cw_last = RingTraits<Ring>::FromBytes(rb);
  return k;
}

// --------------------------------------------------------------------------
//  Explicit instantiation for both rings. ARCHITECTURE section 2 requires the
//  ring width to be a template parameter from day one so the b=64 vs b=128
//  study costs nothing later.
// --------------------------------------------------------------------------
template struct DpfKey<u64>;
template struct DpfKey<u128>;

template std::pair<DpfKey<u64>, DpfKey<u64>>   Gen<u64>(std::uint32_t, u64, std::uint32_t);
template std::pair<DpfKey<u128>, DpfKey<u128>> Gen<u128>(std::uint32_t, u128, std::uint32_t);

template u64  Eval<u64>(const DpfKey<u64>&, std::uint32_t);
template u128 Eval<u128>(const DpfKey<u128>&, std::uint32_t);

template void EvalFull<u64>(const DpfKey<u64>&, Span<u64>);
template void EvalFull<u128>(const DpfKey<u128>&, Span<u128>);

}  // namespace oblivrec
