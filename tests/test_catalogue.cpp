// ==========================================================================
//  test_catalogue.cpp -- the MovieLens-100K catalogue, against real data.
//
//  data/ml-100k/ is gitignored (fetched by scripts/fetch_data.py), so this
//  test SKIPS with a clear message if it is absent rather than failing a
//  fresh clone. That is different from the model-export tests, which have a
//  committed fallback (tests/data/fixedpoint_vectors.txt); there is no
//  practical way to commit a 236 KB fixture copy of licensed-for-research
//  data, so a skip is the honest option here.
// ==========================================================================
#include "oblivrec/catalogue.hpp"
#include "oblivrec_test.hpp"

#include <cstdio>
#include <fstream>
#include <random>
#include <string>

using namespace oblivrec;

namespace {
bool FileExists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}
}  // namespace

int main() {
  std::printf("test_catalogue\n");

  const std::string path = "data/ml-100k/u.item";
  if (!FileExists(path)) {
    std::printf("  SKIP: %s not present (run scripts/fetch_data.py)\n", path.c_str());
    return 0;
  }

  Catalogue cat = Catalogue::LoadMovieLens(path);

  // Known facts about this exact dataset, measured directly rather than
  // assumed. If any of these changes, the dataset itself changed.
  CHECK_MSG(cat.NumItems() == 1682,
            "expected 1682 items, got " + std::to_string(cat.NumItems()));
  CHECK_MSG(cat.DomainBits() == 11,
            "expected domain_bits=11 (1682 pads to 2048), got " +
                std::to_string(cat.DomainBits()));
  CHECK_MSG(cat.DomainSize() == 2048,
            "expected domain_size=2048, got " + std::to_string(cat.DomainSize()));

  // Record width: every slot, real or padding, is exactly kRecordBytes.
  for (std::uint32_t j : {0u, 1u, 1681u, 1682u, 2047u}) {
    CHECK(cat.Record(j).size() == Catalogue::kRecordBytes);
  }

  // Index 0 is item id 1 in the 1-based dataset, "Toy Story (1995)". This is
  // the exact title the PIR test and the demo reconstruct.
  CHECK_MSG(cat.RecordTitle(0) == "Toy Story (1995)",
            "record 0 title is \"" + cat.RecordTitle(0) + "\"");

  // id 267 (0-indexed 266): "unknown", empty date, empty URL. The record
  // that would break a parser assuming a non-empty date.
  CHECK_MSG(cat.RecordTitle(266) == "unknown",
            "record 266 title is \"" + cat.RecordTitle(266) + "\"");

  // id 1242 (0-indexed 1241): the 136-byte width driver, long French title
  // with an inner parenthetical translation.
  const std::string t1241 = cat.RecordTitle(1241);
  CHECK_MSG(t1241.find("Old Lady Who Walked in the Sea") == 0,
            "record 1241 title is \"" + t1241 + "\"");

  // A title with a Latin-1 byte (id 543, 0-indexed 542): "Miserables, Les
  // (1995)" with an accented e (0xE9). Bytes must survive untouched.
  {
    const std::string t542 = cat.RecordTitle(542);
    bool has_e9 = false;
    for (unsigned char c : t542) if (c == 0xE9) has_e9 = true;
    CHECK_MSG(has_e9, "record 542 lost its Latin-1 byte, title is \"" + t542 + "\"");
  }

  // Padding is genuinely zero, at both ends of the padding range.
  for (std::uint32_t j : {1682u, 1900u, 2047u}) {
    const auto rec = cat.Record(j);
    bool all_zero = true;
    for (std::size_t i = 0; i < rec.size(); ++i)
      if (rec[i] != 0) all_zero = false;
    CHECK_MSG(all_zero, "padding record " + std::to_string(j) + " is not all-zero");
    CHECK(cat.RecordTitle(j).empty());
  }

  // Out-of-domain access must throw, not read past the buffer.
  {
    bool threw = false;
    try { cat.Record(cat.DomainSize()); } catch (const std::invalid_argument&) { threw = true; }
    CHECK_MSG(threw, "Record(DomainSize()) did not throw");
  }

  // Every real record round-trips a non-empty id-consistent title except the
  // one known exception (id 267 / index 266, title "unknown", which is a
  // real non-empty string, so actually every one of the 1682 titles is
  // non-empty). Spot check across the whole range with a fixed seed.
  {
    std::mt19937_64 rng(20260908);
    int checked = 0;
    for (int t = 0; t < 200; ++t) {
      const std::uint32_t j =
          static_cast<std::uint32_t>(rng() % cat.NumItems());
      const std::string title = cat.RecordTitle(j);
      CHECK_MSG(!title.empty(),
                "record " + std::to_string(j) + " has an empty title");
      ++checked;
    }
    std::printf("  spot-checked %d real records, all titles non-empty\n", checked);
  }

  return ::oblivrec_test::Report("test_catalogue");
}
