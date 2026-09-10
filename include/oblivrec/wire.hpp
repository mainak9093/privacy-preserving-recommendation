// ==========================================================================
//  wire.hpp -- the S1 client/server message set.
//
//  Header-only, because both src/apps/server.cpp and src/apps/demo.cpp need
//  it and src/apps/ units each link standalone (they have main()).
//
//  ------------------------------------------------------------------------
//  THE PROTOCOL, in the order it runs. The client drives; a server only ever
//  answers. Every message is one Channel frame, so lengths are handled by the
//  framing layer and never appear here.
//
//    client -> Pi   kShares      this party's share of a, and of the mask
//    Pi -> client   kScores      this party's share of the n scores
//        ... repeated per recommendation, and only for P0 and P1 ...
//    client -> Pc   kPirKey      one serialised DPF key
//    Pc -> client   kPirAnswer   W ring words
//    client -> Pi   kBye
//
//  ------------------------------------------------------------------------
//  WHAT CROSSES THE WIRE, AND WHY NONE OF IT IS A SECRET.
//
//  Worth stating precisely, because it is the whole security argument of the
//  networked demo:
//
//    kShares    ONE party's replicated share. Two of the three reconstruct a;
//               one does not. A server sees only its own.
//    kScores    one share of the score vector. Same argument.
//    kPirKey    a DPF key, which is pseudorandom and independent of alpha.
//    kPirAnswer an inner product of pseudorandom coefficients with public
//               data, so it is pseudorandom too.
//
//  Every message length depends only on PUBLIC parameters -- n, d, and
//  domain_bits -- never on a value. That is what the distinguisher experiment
//  in bench/scripts/distinguisher.py checks rather than assumes.
//
//  ------------------------------------------------------------------------
//  ENCODING. Little-endian with explicit shifts throughout, via
//  RingTraits::ToBytes/FromBytes, so the wire format does not depend on the
//  host. Never a memcpy of a struct: ReplicatedShare's layout is the
//  compiler's business, not the protocol's.
// ==========================================================================
#ifndef OBLIVREC_WIRE_HPP
#define OBLIVREC_WIRE_HPP

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "oblivrec/channel.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec/share.hpp"
#include "oblivrec/span.hpp"

namespace oblivrec {

enum class Msg : std::uint8_t {
  kShares = 1,
  kScores = 2,
  kPirKey = 3,
  kPirAnswer = 4,
  kBye = 5,
};

// A message type byte that is not in the enum is malformed input from a peer,
// so it is rejected rather than defaulted. Same discipline as the party byte
// in DpfKey::Deserialize, which accepted 7 as "party 1" until it was fixed.
inline bool IsValidMsg(std::uint8_t t) {
  return t >= static_cast<std::uint8_t>(Msg::kShares) &&
         t <= static_cast<std::uint8_t>(Msg::kBye);
}

inline Msg MsgOf(const std::vector<std::uint8_t>& frame) {
  if (frame.empty()) throw ChannelError("empty frame carries no message type");
  if (!IsValidMsg(frame[0])) {
    throw ChannelError("unknown message type " + std::to_string(frame[0]));
  }
  return static_cast<Msg>(frame[0]);
}

// ---- primitive packing --------------------------------------------------

template <typename Ring>
inline void PutRing(std::vector<std::uint8_t>& out, Ring v) {
  const std::size_t at = out.size();
  out.resize(at + static_cast<std::size_t>(RingTraits<Ring>::kBytes));
  RingTraits<Ring>::ToBytes(v, out.data() + at);
}

template <typename Ring>
inline Ring GetRing(const std::vector<std::uint8_t>& in, std::size_t& at) {
  const std::size_t w = static_cast<std::size_t>(RingTraits<Ring>::kBytes);
  if (at + w > in.size()) {
    throw ChannelError("frame truncated: wanted " + std::to_string(w) +
                       " bytes at offset " + std::to_string(at) + " of " +
                       std::to_string(in.size()));
  }
  const Ring v = RingTraits<Ring>::FromBytes(in.data() + at);
  at += w;
  return v;
}

// ---- kShares ------------------------------------------------------------
// One party's share of the embedding, then of the mask. The mask may be empty,
// which is encoded as a count of zero rather than as an absent field, so the
// parser never has to guess.
template <typename Ring>
inline std::vector<std::uint8_t> EncodeShares(
    const std::vector<ReplicatedShare<Ring>>& a,
    const std::vector<ReplicatedShare<Ring>>& mask) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(Msg::kShares));
  PutRing<Ring>(out, static_cast<Ring>(a.size()));
  PutRing<Ring>(out, static_cast<Ring>(mask.size()));
  for (const auto& s : a) { PutRing<Ring>(out, s.lo); PutRing<Ring>(out, s.hi); }
  for (const auto& s : mask) { PutRing<Ring>(out, s.lo); PutRing<Ring>(out, s.hi); }
  return out;
}

template <typename Ring>
inline void DecodeShares(const std::vector<std::uint8_t>& in,
                         std::vector<ReplicatedShare<Ring>>& a,
                         std::vector<ReplicatedShare<Ring>>& mask) {
  std::size_t at = 1;
  const std::size_t na = static_cast<std::size_t>(GetRing<Ring>(in, at));
  const std::size_t nm = static_cast<std::size_t>(GetRing<Ring>(in, at));
  // Counts arrive over a socket. Bound them before they size an allocation --
  // the framing cap already bounds the frame, so cross-check against it.
  const std::size_t w = static_cast<std::size_t>(RingTraits<Ring>::kBytes);
  if ((na + nm) * 2 * w + 1 + 2 * w > in.size()) {
    throw ChannelError("kShares declares more entries than the frame holds");
  }
  a.resize(na);
  mask.resize(nm);
  for (auto& s : a) { s.lo = GetRing<Ring>(in, at); s.hi = GetRing<Ring>(in, at); }
  for (auto& s : mask) { s.lo = GetRing<Ring>(in, at); s.hi = GetRing<Ring>(in, at); }
}

// ---- kScores / kPirAnswer: a flat vector of ring words -------------------
template <typename Ring>
inline std::vector<std::uint8_t> EncodeWords(Msg type, Span<const Ring> words) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(type));
  PutRing<Ring>(out, static_cast<Ring>(words.size()));
  for (std::size_t i = 0; i < words.size(); ++i) PutRing<Ring>(out, words[i]);
  return out;
}

template <typename Ring>
inline void DecodeWords(const std::vector<std::uint8_t>& in,
                        std::vector<Ring>& out) {
  std::size_t at = 1;
  const std::size_t n = static_cast<std::size_t>(GetRing<Ring>(in, at));
  const std::size_t w = static_cast<std::size_t>(RingTraits<Ring>::kBytes);
  if (n * w + 1 + w > in.size()) {
    throw ChannelError("word vector declares more entries than the frame holds");
  }
  out.resize(n);
  for (auto& v : out) v = GetRing<Ring>(in, at);
}

// ---- kPirKey ------------------------------------------------------------
inline std::vector<std::uint8_t> EncodeKeyBytes(
    const std::vector<std::uint8_t>& key) {
  std::vector<std::uint8_t> out;
  out.reserve(key.size() + 1);
  out.push_back(static_cast<std::uint8_t>(Msg::kPirKey));
  out.insert(out.end(), key.begin(), key.end());
  return out;
}

inline Span<const std::uint8_t> KeyBytesOf(const std::vector<std::uint8_t>& in) {
  if (in.size() < 2) throw ChannelError("kPirKey frame carries no key");
  return Span<const std::uint8_t>(in.data() + 1, in.size() - 1);
}

inline std::vector<std::uint8_t> EncodeBye() {
  return std::vector<std::uint8_t>{static_cast<std::uint8_t>(Msg::kBye)};
}

}  // namespace oblivrec
#endif  // OBLIVREC_WIRE_HPP
