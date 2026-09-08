// ==========================================================================
//  catalogue.hpp -- the fixed-width, PIR-servable MovieLens-100K catalogue.
//
//  RECORD SCHEMA. u.item is 24 '|'-separated fields; we keep four groups and
//  drop two that add nothing:
//    - field 3 (video release date) is EMPTY on all 1682 lines. Confirmed by
//      direct measurement, not assumed.
//    - field 4 (IMDb URL) is the single largest field and is not used by
//      anything downstream. Dropping it takes the longest real record from
//      265 bytes to 136, which is what makes L=256 (ARCHITECTURE section 2)
//      comfortable rather than tight.
//
//  Kept, in this order: id | title | date | 19 genre flags.
//
//  THREE RECORDS BREAK A NAIVE PARSER, each handled explicitly rather than by
//  a silent fallback, because a silent fallback is how a catalogue quietly
//  loses a row:
//    - id 267: title "unknown", empty date, empty URL. Not an error in the
//      source data, just a record with less in it. Encoded with an empty date
//      field, which the fixed-width record already supports.
//    - id 1242: the width driver at 136 bytes with the URL and empty field
//      dropped. Verified below rather than assumed to fit.
//    - the release date "4-Feb-1971" (id unlisted, single-digit day) does not
//      match the DD-Mon-YYYY pattern every other date follows. We do not
//      parse dates into a structured type at all, so this never mattered:
//      the date field is carried as opaque text.
//
//  DOMAIN PADDING. 1682 items pad to 2048 (domain_bits = 11), which is the
//  figure the report and the DPF benchmarks both use for the MovieLens-100K
//  demo. Padding records are all-zero, which leaks nothing because every
//  record is the same width regardless of content.
// ==========================================================================
#ifndef OBLIVREC_CATALOGUE_HPP
#define OBLIVREC_CATALOGUE_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "oblivrec/span.hpp"

namespace oblivrec {

class Catalogue {
 public:
  // Fixed record width, ARCHITECTURE section 2. Measured max real record
  // (id 1242, id|title|date|19 flags, URL and empty field dropped) is 136
  // bytes, so this has 120 bytes of headroom.
  static constexpr std::size_t kRecordBytes = 256;

  // Load and validate data/ml-100k/u.item. Throws std::invalid_argument on
  // any line that does not have exactly 24 '|'-separated fields, and
  // std::runtime_error if the file cannot be opened or is empty. Every
  // record is checked to fit kRecordBytes; a future edit to the dataset that
  // grows a title past the budget fails loudly here rather than truncating
  // silently at PIR-answer time.
  static Catalogue LoadMovieLens(const std::string& u_item_path);

  // A fixed-width, null-padded record. Index j in [0, DomainSize()).
  // Indices >= NumItems() and < DomainSize() are padding: all-zero bytes.
  Span<const std::uint8_t> Record(std::uint32_t j) const;

  // For a human-readable index: decode a record's title back out, trimming
  // the trailing zero padding. Empty string for a padding record.
  std::string RecordTitle(std::uint32_t j) const;

  std::uint32_t NumItems() const { return num_items_; }
  std::uint32_t DomainSize() const { return domain_size_; }
  std::uint32_t DomainBits() const { return domain_bits_; }

 private:
  Catalogue() = default;

  std::uint32_t num_items_ = 0;
  std::uint32_t domain_size_ = 0;
  std::uint32_t domain_bits_ = 0;
  std::vector<std::uint8_t> flat_;   // domain_size_ * kRecordBytes bytes
};

}  // namespace oblivrec
#endif  // OBLIVREC_CATALOGUE_HPP
