// ==========================================================================
//  test_pir.cpp -- the DPF-PIR read layer, against the real catalogue.
//
//  The correctness oracle (RULES.md C3) is Catalogue::Record(j) itself: a
//  private fetch of record j must return byte-for-byte what a direct
//  cleartext lookup returns. Anything less is not a working delivery layer.
//
//  Skips if data/ml-100k/ is absent, as test_catalogue does.
// ==========================================================================
#include "oblivrec/catalogue.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec_test.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

bool FileExists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

// One full private fetch: client makes keys, two servers answer, client
// recombines. Returns the reconstructed record bytes.
template <typename Ring>
std::vector<std::uint8_t> PrivateFetch(const PirServer<Ring>& p0,
                                       const PirServer<Ring>& p1,
                                       const PirClient<Ring>& client,
                                       std::uint32_t alpha) {
  auto keys = client.Query(alpha);
  const std::size_t W = RecordWords<Ring>();
  std::vector<Ring> a0(W), a1(W);
  p0.Answer(keys.first, Span<Ring>(a0.data(), W));
  p1.Answer(keys.second, Span<Ring>(a1.data(), W));
  return PirClient<Ring>::ReconstructBytes(
      Span<const Ring>(a0.data(), W), Span<const Ring>(a1.data(), W));
}

template <typename Ring>
void RunForRing(const Catalogue& cat, const char* ring_name, int trials) {
  PirServer<Ring> p0(cat), p1(cat);
  PirClient<Ring> client(cat.DomainBits());

  // THE EXIT-CRITERION CASE. Record 0 is "Toy Story (1995)". Fetch it
  // privately and require byte-exact agreement with a cleartext lookup.
  {
    const auto got = PrivateFetch<Ring>(p0, p1, client, 0);
    const auto want = cat.Record(0);
    bool same = got.size() == want.size();
    if (same) {
      for (std::size_t i = 0; i < want.size(); ++i)
        if (got[i] != want[i]) { same = false; break; }
    }
    CHECK_MSG(same, std::string(ring_name) +
                        ": private fetch of record 0 does not match cleartext");

    // And that it really is the title we expect, decoded from the fetched
    // bytes rather than from the catalogue object.
    const std::uint8_t title_len = got[4];
    const std::string title(reinterpret_cast<const char*>(&got[5]), title_len);
    CHECK_MSG(title == "Toy Story (1995)",
              std::string(ring_name) + ": fetched title is \"" + title + "\"");
    if (title == "Toy Story (1995)") {
      std::printf("  %-4s fetched record 0 privately: \"%s\"\n", ring_name,
                  title.c_str());
    }
  }

  // Random indices across the whole domain, including padding slots.
  {
    std::mt19937_64 rng(20260908);
    int mismatches = 0;
    for (int t = 0; t < trials; ++t) {
      const std::uint32_t alpha =
          static_cast<std::uint32_t>(rng() % cat.DomainSize());
      const auto got = PrivateFetch<Ring>(p0, p1, client, alpha);
      const auto want = cat.Record(alpha);
      for (std::size_t i = 0; i < want.size(); ++i) {
        if (got[i] != want[i]) { ++mismatches; break; }
      }
    }
    CHECK_MSG(mismatches == 0,
              std::string(ring_name) + ": " + std::to_string(mismatches) +
                  " of " + std::to_string(trials) + " private fetches wrong");
    std::printf("  %-4s %d random private fetches, all byte-exact\n",
                ring_name, trials);
  }

  // Boundary indices: first, last real, first padding, last padding.
  {
    const std::uint32_t idx[] = {0, cat.NumItems() - 1, cat.NumItems(),
                                 cat.DomainSize() - 1};
    for (std::uint32_t alpha : idx) {
      const auto got = PrivateFetch<Ring>(p0, p1, client, alpha);
      const auto want = cat.Record(alpha);
      bool same = true;
      for (std::size_t i = 0; i < want.size(); ++i)
        if (got[i] != want[i]) { same = false; break; }
      CHECK_MSG(same, std::string(ring_name) + ": boundary fetch at " +
                          std::to_string(alpha) + " is wrong");
    }
  }

  // A padding slot must come back all-zero, not garbage.
  {
    const auto got = PrivateFetch<Ring>(p0, p1, client, cat.DomainSize() - 1);
    bool all_zero = true;
    for (std::uint8_t b : got) if (b != 0) all_zero = false;
    CHECK_MSG(all_zero,
              std::string(ring_name) + ": padding slot did not fetch as zero");
  }

  // A single server's answer must not reveal the record.
  //
  // CARE IS NEEDED STATING THIS. A naive "no word of the share equals the
  // plaintext" assertion FAILS, and the first version of this test did. The
  // reason is not a leak: records are padded to a fixed 256 bytes while real
  // content never exceeds about 117, and the trailing genre flags are almost
  // always zero, so the upper words are identically zero across the ENTIRE
  // catalogue. A linear combination of all-zero values is zero, so share and
  // plaintext agree there trivially. That is public structure and it is
  // constant across every alpha, so it distinguishes nothing.
  //
  // The property worth asserting is about the words that actually carry
  // content: those must not match.
  {
    const std::size_t W = RecordWords<Ring>();

    // Which words vary across the catalogue at all? Computed from the data
    // rather than hardcoded, so a schema change cannot quietly weaken this.
    std::vector<bool> varies(W, false);
    for (std::uint32_t j = 0; j < cat.DomainSize(); ++j) {
      const auto rec = cat.Record(j);
      for (std::size_t w = 0; w < W; ++w) {
        if (RingTraits<Ring>::FromBytes(&rec[w * RingTraits<Ring>::kBytes]) != Ring(0))
          varies[w] = true;
      }
    }
    std::size_t n_varies = 0;
    for (std::size_t w = 0; w < W; ++w) if (varies[w]) ++n_varies;

    auto keys = client.Query(0);
    std::vector<Ring> a0(W);
    p0.Answer(keys.first, Span<Ring>(a0.data(), W));

    const auto want = cat.Record(0);
    int equal_content_words = 0;
    for (std::size_t w = 0; w < W; ++w) {
      if (!varies[w]) continue;   // structurally zero, see above
      const Ring truth = RingTraits<Ring>::FromBytes(&want[w * RingTraits<Ring>::kBytes]);
      if (a0[w] == truth) ++equal_content_words;
    }
    CHECK_MSG(equal_content_words == 0,
              std::string(ring_name) + ": " + std::to_string(equal_content_words) +
                  " content-bearing words of one server's answer equal the "
                  "plaintext record");
    std::printf("  %-4s %zu of %zu words carry content; share matches none of them\n",
                ring_name, n_varies, W);
  }

  // Mismatched domain_bits must be rejected rather than silently reading the
  // wrong amount of the expansion.
  {
    PirClient<Ring> wrong(cat.DomainBits() - 1);
    auto keys = wrong.Query(0);
    const std::size_t W = RecordWords<Ring>();
    std::vector<Ring> a(W);
    bool threw = false;
    try { p0.Answer(keys.first, Span<Ring>(a.data(), W)); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK_MSG(threw, std::string(ring_name) +
                         ": Answer accepted a key with the wrong domain_bits");
  }
}

}  // namespace

int main() {
  std::printf("test_pir\n");

  const std::string path = "data/ml-100k/u.item";
  if (!FileExists(path)) {
    std::printf("  SKIP: %s not present (run scripts/fetch_data.py)\n", path.c_str());
    return 0;
  }

  Catalogue cat = Catalogue::LoadMovieLens(path);
  std::printf("  catalogue: %u items, domain %u (%u bits), %zu B records\n",
              cat.NumItems(), cat.DomainSize(), cat.DomainBits(),
              Catalogue::kRecordBytes);

  // 60 trials at b=64. Each fetch is two whole-domain expansions over 2048
  // slots, so this is the dominant cost in the suite and is kept modest.
  RunForRing<u64>(cat, "u64", 60);
  RunForRing<u128>(cat, "u128", 20);

  return ::oblivrec_test::Report("test_pir");
}
