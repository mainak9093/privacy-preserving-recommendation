// ==========================================================================
//  pir.cpp -- two-server DPF-PIR read. See pir.hpp for the construction and
//  for why it is oblivious.
// ==========================================================================
#include "oblivrec/pir.hpp"

#include <cstring>
#include <stdexcept>

namespace oblivrec {

template <typename Ring>
PirServer<Ring>::PirServer(const Catalogue& cat)
    : domain_bits_(cat.DomainBits()), domain_size_(cat.DomainSize()) {
  const std::size_t W = RecordWords<Ring>();
  static_assert(Catalogue::kRecordBytes % RingTraits<Ring>::kBytes == 0,
                "record width must divide evenly into ring words");

  // Unpack every record into ring words ONCE, at construction. The hot loop
  // in Answer is then a flat array walk. Doing this per query would make the
  // measured PIR cost mostly byte-shuffling rather than the actual read.
  words_.assign(std::size_t(domain_size_) * W, Ring(0));
  for (std::uint32_t j = 0; j < domain_size_; ++j) {
    const auto rec = cat.Record(j);
    for (std::size_t w = 0; w < W; ++w) {
      words_[std::size_t(j) * W + w] =
          RingTraits<Ring>::FromBytes(&rec[w * RingTraits<Ring>::kBytes]);
    }
  }
}

template <typename Ring>
void PirServer<Ring>::Answer(const DpfKey<Ring>& key, Span<Ring> out) const {
  const std::size_t W = RecordWords<Ring>();
  if (out.size() != W) {
    throw std::invalid_argument("PirServer::Answer: out must be RecordWords()");
  }
  if (key.domain_bits != domain_bits_) {
    throw std::invalid_argument(
        "PirServer::Answer: key domain_bits " + std::to_string(key.domain_bits) +
        " does not match catalogue domain_bits " + std::to_string(domain_bits_));
  }

  // Expand the key across the whole domain.
  std::vector<Ring> e(domain_size_);
  EvalFull<Ring>(key, Span<Ring>(e.data(), e.size()));

  for (std::size_t w = 0; w < W; ++w) out[w] = Ring(0);

  // The oblivious inner product. Every index is touched, in order, and the
  // coefficient e[j] is a pseudorandom share at every index rather than zero
  // at all but one, so there is no data-dependent work here at all.
  for (std::uint32_t j = 0; j < domain_size_; ++j) {
    const Ring c = e[j];
    const Ring* row = &words_[std::size_t(j) * W];
    for (std::size_t w = 0; w < W; ++w) {
      out[w] = static_cast<Ring>(out[w] + static_cast<Ring>(c * row[w]));
    }
  }
}

template <typename Ring>
std::pair<DpfKey<Ring>, DpfKey<Ring>> PirClient<Ring>::Query(
    std::uint32_t alpha) const {
  // beta = 1 so the difference of the two expansions is the selector vector
  // exactly, with no scaling factor to divide back out.
  return Gen<Ring>(alpha, Ring(1), domain_bits_);
}

template <typename Ring>
void PirClient<Ring>::Reconstruct(Span<const Ring> a0, Span<const Ring> a1,
                                  Span<Ring> out) {
  if (a0.size() != a1.size() || a0.size() != out.size()) {
    throw std::invalid_argument("PirClient::Reconstruct: size mismatch");
  }
  // DIFFERENCE, not sum. See the sign-convention note in dpf.hpp.
  for (std::size_t w = 0; w < out.size(); ++w) {
    out[w] = static_cast<Ring>(a0[w] - a1[w]);
  }
}

template <typename Ring>
std::vector<std::uint8_t> PirClient<Ring>::ReconstructBytes(
    Span<const Ring> a0, Span<const Ring> a1) {
  const std::size_t W = RecordWords<Ring>();
  if (a0.size() != W || a1.size() != W) {
    throw std::invalid_argument("PirClient::ReconstructBytes: expected RecordWords()");
  }
  std::vector<Ring> words(W);
  Reconstruct(a0, a1, Span<Ring>(words.data(), words.size()));

  std::vector<std::uint8_t> bytes(Catalogue::kRecordBytes);
  for (std::size_t w = 0; w < W; ++w) {
    RingTraits<Ring>::ToBytes(words[w], &bytes[w * RingTraits<Ring>::kBytes]);
  }
  return bytes;
}

// --------------------------------------------------------------------------
//  Explicit instantiation, matching dpf.cpp. A ring added there must be
//  added here too.
// --------------------------------------------------------------------------
template class PirServer<u64>;
template class PirServer<u128>;
template class PirClient<u64>;
template class PirClient<u128>;

}  // namespace oblivrec
