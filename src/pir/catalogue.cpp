// ==========================================================================
//  catalogue.cpp -- load and pack data/ml-100k/u.item into fixed-width
//  records. See catalogue.hpp for the schema and the rationale.
//
//  RECORD LAYOUT within each kRecordBytes-byte slot:
//    offset 0        id           u32, little-endian
//    offset 4        title_len    u8   (observed max 81, so u8 is plenty)
//    offset 5        title        title_len raw Latin-1 bytes
//    offset 5+tl     date_len     u8
//    offset 6+tl     date         date_len raw ASCII bytes (may be 0, id 267)
//    offset 6+tl+dl  genres       19 bytes, each 0x00 or 0x01
//    remainder       zero padding
//
//  Explicit lengths rather than a delimiter, because a delimiter risks
//  colliding with a byte inside a Latin-1 title, and because it makes
//  RecordTitle extraction unambiguous without re-parsing the pipe format.
// ==========================================================================
#include "oblivrec/catalogue.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace oblivrec {
namespace {

constexpr int kNumGenres = 19;

// u.item fields, 0-indexed, after the split on '|'. We read id/title/date
// directly and the 19 genre flags starting here; fields 3 (video date,
// always empty) and 4 (IMDb URL) are read past and discarded.
constexpr int kFieldId = 0;
constexpr int kFieldTitle = 1;
constexpr int kFieldDate = 2;
constexpr int kFieldGenreStart = 5;
constexpr int kExpectedFields = 24;   // id..title..date..videodate..url..19 genres

std::vector<std::string> SplitPipes(const std::string& line) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (;;) {
    const std::size_t pos = line.find('|', start);
    if (pos == std::string::npos) {
      out.push_back(line.substr(start));
      break;
    }
    out.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

std::uint32_t DomainBitsFor(std::uint32_t n) {
  std::uint32_t bits = 0;
  std::uint32_t cap = 1;
  while (cap < n) { cap <<= 1; ++bits; }
  return bits == 0 ? 1 : bits;   // domain_bits >= 1, matching Gen's contract
}

void PackRecord(std::uint8_t* slot, std::uint32_t id,
                const std::string& title, const std::string& date,
                const std::vector<int>& genres, std::size_t line_no) {
  std::memset(slot, 0, Catalogue::kRecordBytes);

  if (title.size() > 255) {
    throw std::invalid_argument(
        "catalogue: title exceeds 255 bytes at line " + std::to_string(line_no));
  }
  if (date.size() > 255) {
    throw std::invalid_argument(
        "catalogue: date exceeds 255 bytes at line " + std::to_string(line_no));
  }
  const std::size_t needed = 4 + 1 + title.size() + 1 + date.size() +
                             static_cast<std::size_t>(kNumGenres);
  if (needed > Catalogue::kRecordBytes) {
    // The check that would catch a future dataset edit growing a title past
    // the budget, rather than truncating it silently at PIR-answer time.
    throw std::invalid_argument(
        "catalogue: record " + std::to_string(id) + " needs " +
        std::to_string(needed) + " bytes, kRecordBytes is " +
        std::to_string(Catalogue::kRecordBytes));
  }

  std::size_t p = 0;
  slot[p++] = static_cast<std::uint8_t>(id & 0xff);
  slot[p++] = static_cast<std::uint8_t>((id >> 8) & 0xff);
  slot[p++] = static_cast<std::uint8_t>((id >> 16) & 0xff);
  slot[p++] = static_cast<std::uint8_t>((id >> 24) & 0xff);

  slot[p++] = static_cast<std::uint8_t>(title.size());
  std::memcpy(slot + p, title.data(), title.size());
  p += title.size();

  slot[p++] = static_cast<std::uint8_t>(date.size());
  std::memcpy(slot + p, date.data(), date.size());
  p += date.size();

  for (int g = 0; g < kNumGenres; ++g) {
    slot[p++] = static_cast<std::uint8_t>(genres[static_cast<std::size_t>(g)] ? 1 : 0);
  }
  // Remaining bytes to kRecordBytes are already zero from the memset above.
}

}  // namespace

Catalogue Catalogue::LoadMovieLens(const std::string& u_item_path) {
  // u.item is Latin-1, not UTF-8 (nine titles contain bytes like 0xE9 that
  // make a UTF-8 decode raise). We never re-encode: bytes go in, bytes come
  // out, and interpretation is left to the caller.
  std::ifstream in(u_item_path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("catalogue: cannot open " + u_item_path);
  }

  Catalogue cat;
  std::vector<std::vector<std::uint8_t>> records;   // built up, then flattened

  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    const auto fields = SplitPipes(line);
    if (fields.size() != static_cast<std::size_t>(kExpectedFields)) {
      throw std::invalid_argument(
          "catalogue: line " + std::to_string(line_no) + " has " +
          std::to_string(fields.size()) + " fields, expected " +
          std::to_string(kExpectedFields) +
          ". u.item's schema is assumed fixed; if it changed, the parser "
          "needs updating, not a silent skip.");
    }

    std::uint32_t id = 0;
    try {
      id = static_cast<std::uint32_t>(std::stoul(fields[kFieldId]));
    } catch (const std::exception&) {
      throw std::invalid_argument(
          "catalogue: line " + std::to_string(line_no) + " has a non-numeric id: \"" +
          fields[kFieldId] + "\"");
    }

    // id 267 ("unknown") has an empty date. That is real data, not an error:
    // the record is simply carried with a zero-length date field, which the
    // explicit-length encoding already supports without a special case here.
    const std::string& title = fields[kFieldTitle];
    const std::string& date = fields[kFieldDate];

    std::vector<int> genres(static_cast<std::size_t>(kNumGenres));
    for (int g = 0; g < kNumGenres; ++g) {
      const std::string& f = fields[static_cast<std::size_t>(kFieldGenreStart + g)];
      if (f != "0" && f != "1") {
        throw std::invalid_argument(
            "catalogue: line " + std::to_string(line_no) + " genre flag " +
            std::to_string(g) + " is \"" + f + "\", expected 0 or 1");
      }
      genres[static_cast<std::size_t>(g)] = (f == "1") ? 1 : 0;
    }

    std::vector<std::uint8_t> slot(Catalogue::kRecordBytes);
    PackRecord(slot.data(), id, title, date, genres, line_no);
    records.push_back(std::move(slot));
  }

  if (records.empty()) {
    throw std::runtime_error("catalogue: " + u_item_path + " contained no records");
  }

  cat.num_items_ = static_cast<std::uint32_t>(records.size());
  cat.domain_bits_ = DomainBitsFor(cat.num_items_);
  cat.domain_size_ = std::uint32_t(1) << cat.domain_bits_;

  cat.flat_.assign(std::size_t(cat.domain_size_) * kRecordBytes, 0);
  for (std::uint32_t i = 0; i < cat.num_items_; ++i) {
    std::memcpy(&cat.flat_[std::size_t(i) * kRecordBytes], records[i].data(),
               kRecordBytes);
  }
  // Records at [num_items_, domain_size_) stay zero: real padding, leaking
  // nothing, since every slot is read from and every slot is the same width
  // regardless of whether it holds a real record.

  return cat;
}

Span<const std::uint8_t> Catalogue::Record(std::uint32_t j) const {
  if (j >= domain_size_) {
    throw std::invalid_argument(
        "catalogue: index " + std::to_string(j) + " outside domain size " +
        std::to_string(domain_size_));
  }
  return Span<const std::uint8_t>(&flat_[std::size_t(j) * kRecordBytes],
                                  kRecordBytes);
}

std::string Catalogue::RecordTitle(std::uint32_t j) const {
  const auto rec = Record(j);
  if (j >= num_items_) return "";   // padding slot

  std::size_t p = 4;   // skip the 4-byte id
  const std::uint8_t title_len = rec[p++];
  std::string title(reinterpret_cast<const char*>(&rec[p]), title_len);
  return title;
}

}  // namespace oblivrec
