// ==========================================================================
//  pir.hpp -- two-server DPF-PIR read over the fixed-width catalogue.
//
//  This is the layer NUDGE explicitly delegates to "other means" and that
//  PIRSONA supplies. It is the project's contribution over NUDGE, and it is
//  the reason the DPF exists.
//
//  ------------------------------------------------------------------------
//  HOW IT WORKS. The client wants record alpha without either server
//  learning alpha. It generates a DPF key pair for the point function that is
//  1 at alpha and 0 elsewhere, and sends one key to each server. Each server
//  expands its key over the WHOLE domain and takes an inner product against
//  the catalogue:
//
//      answer_b[w] = sum over j of  EvalFull(k_b)[j] * D[j][w]
//
//  and because EvalFull(k0)[j] - EvalFull(k1)[j] is 1 at j = alpha and 0
//  everywhere else, the difference of the two answers is exactly D[alpha][w].
//
//  NOTE THE DIFFERENCE CONVENTION. This project omits the (-1)^party factor
//  (see dpf.hpp), so reconstruction subtracts rather than sums. ARCHITECTURE
//  section 7.2 said "sum" until 2026-09-08; the code was always difference
//  and the prose was corrected to match.
//
//  ------------------------------------------------------------------------
//  WHY IT IS OBLIVIOUS. Two properties, both worth stating precisely because
//  they are what the security claim rests on:
//
//    1. The server touches EVERY record, in index order, with no
//       data-dependent branch. The loop bound is the domain size and nothing
//       inside it depends on alpha.
//    2. Each expanded coefficient e_j is a SHARE, so it is pseudorandom at
//       every index rather than zero at all but one. There is nothing for a
//       compiler or a cache-timing observer to distinguish, even in
//       principle. This is stronger than "the code has no branch": there is
//       no data pattern either.
//
//  ------------------------------------------------------------------------
//  WHICH TWO OF THREE SERVERS. The system has three servers but the DPF is
//  (2,2). Decided 2026-09-08: P0 and P1 hold the PIR keys; all three hold the
//  catalogue, which is public data anyway. Recorded in the Decisions Log.
// ==========================================================================
#ifndef OBLIVREC_PIR_HPP
#define OBLIVREC_PIR_HPP

#include <cstdint>
#include <utility>
#include <vector>

#include "oblivrec/catalogue.hpp"
#include "oblivrec/dpf.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec/span.hpp"

namespace oblivrec {

// A catalogue record reinterpreted as ring words, which is the form the
// inner product operates on. 256 bytes at b=64 is 32 words.
template <typename Ring>
constexpr std::size_t RecordWords() {
  return Catalogue::kRecordBytes / static_cast<std::size_t>(RingTraits<Ring>::kBytes);
}

// One server's view: the catalogue, pre-unpacked into ring words so the hot
// loop is a flat array walk rather than repeated byte decoding.
template <typename Ring>
class PirServer {
 public:
  explicit PirServer(const Catalogue& cat);

  // Expand the key over the whole domain and inner-product against the
  // catalogue. out.size() must be RecordWords<Ring>().
  void Answer(const DpfKey<Ring>& key, Span<Ring> out) const;

  std::uint32_t DomainBits() const { return domain_bits_; }
  std::uint32_t DomainSize() const { return domain_size_; }

 private:
  std::uint32_t domain_bits_ = 0;
  std::uint32_t domain_size_ = 0;
  std::vector<Ring> words_;   // domain_size_ * RecordWords<Ring>()
};

// The client side. Only the client calls Gen, as dpf.hpp notes.
template <typename Ring>
class PirClient {
 public:
  explicit PirClient(std::uint32_t domain_bits) : domain_bits_(domain_bits) {}

  // Keys for fetching record alpha. Send .first to P0 and .second to P1.
  std::pair<DpfKey<Ring>, DpfKey<Ring>> Query(std::uint32_t alpha) const;

  // Recombine the two answers into the record, as ring words.
  // DIFFERENCE, not sum. See the header comment.
  static void Reconstruct(Span<const Ring> a0, Span<const Ring> a1,
                          Span<Ring> out);

  // Recombine straight into bytes, which is what a caller actually wants.
  static std::vector<std::uint8_t> ReconstructBytes(Span<const Ring> a0,
                                                    Span<const Ring> a1);

 private:
  std::uint32_t domain_bits_;
};

}  // namespace oblivrec
#endif  // OBLIVREC_PIR_HPP
