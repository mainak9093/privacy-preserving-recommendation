// ==========================================================================
//  demo.cpp -- the Phase 2 exit criterion, end to end.
//
//    ./demo --user 42 --k 10
//
//  What it demonstrates, in order:
//    1. The user's embedding is split across three servers. No server holds
//       it.
//    2. Each server scores the whole catalogue locally against the PUBLIC
//       item matrix. No communication, no multiplication protocol, because
//       a public matrix times a shared vector is a local linear map.
//    3. The user reconstructs the score vector and takes the top k in the
//       clear, on their own machine. The servers never learn the ranking,
//       because they never compute it.
//    4. For each recommended item the user runs a two-server DPF-PIR fetch.
//       Neither server learns which record was read, and the two answers
//       differ by exactly the record.
//
//  Step 4 is the part NUDGE delegates to "other means" and is this project's
//  contribution over it.
//
//  What this is NOT: private TRAINING. The model here was factorised in the
//  clear by model/export.py. Training under secret sharing is S2, scheduled
//  after the mid-term. The demo says so on stdout rather than letting the
//  output imply more than was built.
// ==========================================================================
#include "oblivrec/catalogue.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/serve.hpp"
#include "oblivrec/share.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

bool ReadI64(const std::string& path, std::vector<std::int64_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const std::streamoff bytes = f.tellg();
  f.seekg(0, std::ios::beg);
  if (bytes <= 0 || bytes % 8 != 0) return false;
  out.resize(static_cast<std::size_t>(bytes / 8));
  for (auto& v : out) {
    std::uint8_t b[8];
    f.read(reinterpret_cast<char*>(b), 8);
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    v = static_cast<std::int64_t>(u);
  }
  return true;
}

// The user's already-rated items, from the training split. In the real
// system this set never leaves the client; here the demo plays the client, so
// it reads the split directly and shares the mask before handing it out.
//
// u1.base is TAB-separated "user item rating timestamp" with 1-BASED ids, and
// everything else in this project is 0-based. Converting in one place, here,
// rather than at each use.
bool ReadSeen(const std::string& path, std::uint32_t user, std::uint32_t n,
              std::vector<std::uint8_t>& seen) {
  std::ifstream f(path);
  if (!f) return false;
  seen.assign(n, 0);
  long u = 0, it = 0, r = 0, ts = 0;
  std::size_t rows = 0;
  while (f >> u >> it >> r >> ts) {
    ++rows;
    if (u - 1 == static_cast<long>(user) && it >= 1 &&
        it <= static_cast<long>(n)) {
      seen[static_cast<std::size_t>(it - 1)] = 1;
    }
  }
  return rows > 0;
}

std::string TitleFromRecord(const std::vector<std::uint8_t>& rec) {
  if (rec.size() < 5) return "";
  const std::uint8_t len = rec[4];
  if (std::size_t(5) + len > rec.size()) return "";
  return std::string(reinterpret_cast<const char*>(&rec[5]), len);
}

}  // namespace

int main(int argc, char** argv) {
  std::uint32_t user = 42, k = 10;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
      user = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--k") == 0 && i + 1 < argc) {
      k = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else {
      std::fprintf(stderr, "usage: demo [--user N] [--k N]\n");
      return 2;
    }
  }

  std::vector<std::int64_t> A, B;
  if (!ReadI64("model/out/A.bin", A) || !ReadI64("model/out/B.bin", B)) {
    std::fprintf(stderr,
                 "demo: model/out/ is missing. Run:  py -3.13 model/export.py\n");
    return 1;
  }

  Catalogue cat = Catalogue::LoadMovieLens("data/ml-100k/u.item");
  const std::uint32_t d = 16;
  const std::uint32_t n = cat.NumItems();
  const std::uint32_t m = static_cast<std::uint32_t>(A.size() / d);

  if (user >= m) {
    std::fprintf(stderr, "demo: user %u out of range (m = %u)\n", user, m);
    return 2;
  }
  if (B.size() != std::size_t(d) * n) {
    std::fprintf(stderr, "demo: B.bin has %zu entries, expected %u\n",
                 B.size(), d * n);
    return 1;
  }

  std::printf("OblivRec demo. user=%u k=%u  catalogue=%u items (domain %u)\n",
              user, k, n, cat.DomainSize());
  std::printf("Model trained IN THE CLEAR (private training is S2, after the mid-term).\n\n");

  // ---- 1. Split the user embedding across three servers -----------------
  std::vector<std::vector<ReplicatedShare<u64>>> a_party(3);
  for (auto& p : a_party) p.resize(d);
  for (std::uint32_t i = 0; i < d; ++i) {
    auto s = Split<u64>(static_cast<u64>(A[std::size_t(user) * d + i]));
    for (int p = 0; p < 3; ++p) a_party[static_cast<std::size_t>(p)][i] = s[static_cast<std::size_t>(p)];
  }
  std::printf("[1] user embedding split 2-of-3 across P0,P1,P2. No server holds it.\n");

  // ---- 1b. The seen-item mask, also shared ------------------------------
  std::vector<std::uint8_t> seen;
  std::vector<std::vector<ReplicatedShare<u64>>> mask;
  const bool have_seen = ReadSeen("data/ml-100k/u1.base", user, n, seen);
  if (have_seen) {
    std::uint32_t nseen = 0;
    for (auto s : seen) nseen += s;
    mask = BuildMaskShares<u64>(seen, n);
    std::printf("    mask over %u already-rated items shared the same way;\n"
                "    no server learns WHICH items those are.\n", nseen);
  } else {
    std::printf("    (no u1.base found: running unmasked, so already-rated\n"
                "     items may appear in the ranking)\n");
  }

  // ---- 2. Each server scores locally against the public B ---------------
  std::vector<u64> Bring(B.size());
  for (std::size_t i = 0; i < B.size(); ++i) Bring[i] = static_cast<u64>(B[i]);

  std::vector<std::vector<ReplicatedShare<u64>>> score_party(3);
  for (int p = 0; p < 3; ++p) {
    ScoreShares<u64>(a_party[static_cast<std::size_t>(p)],
                     Span<const u64>(Bring.data(), Bring.size()), d, n,
                     have_seen ? mask[static_cast<std::size_t>(p)]
                               : std::vector<ReplicatedShare<u64>>{},
                     score_party[static_cast<std::size_t>(p)]);
  }
  std::printf("[2] each server scored %u items locally. B is public, so this is\n"
              "    a linear map on shares: no communication, no rounds.\n", n);

  // ---- 3. User reconstructs and takes top-k in the clear ----------------
  std::vector<u64> scores_ring;
  ReconstructScores<u64>(score_party, scores_ring);
  std::vector<std::int64_t> scores(n);
  for (std::uint32_t j = 0; j < n; ++j)
    scores[j] = static_cast<std::int64_t>(scores_ring[j]);

  const auto top = TopK(Span<const std::int64_t>(scores.data(), n), k);
  std::printf("[3] user reconstructed the score vector and took the top %u locally.\n"
              "    The servers never computed a ranking, so they cannot know it.\n\n", k);

  // ---- 4. Private fetch of each recommended record ----------------------
  PirServer<u64> p0(cat), p1(cat);
  PirClient<u64> client(cat.DomainBits());
  const std::size_t W = RecordWords<u64>();

  std::printf("[4] fetching each recommendation by two-server DPF-PIR:\n\n");
  std::printf("    %-4s %-6s %-22s %s\n", "rank", "item", "score (2t-scaled)", "title (fetched privately)");

  int mismatches = 0;
  for (std::uint32_t r = 0; r < top.size(); ++r) {
    const std::uint32_t j = top[r];
    auto keys = client.Query(j);
    std::vector<u64> a0(W), a1(W);
    p0.Answer(keys.first, Span<u64>(a0.data(), W));
    p1.Answer(keys.second, Span<u64>(a1.data(), W));
    const auto rec = PirClient<u64>::ReconstructBytes(
        Span<const u64>(a0.data(), W), Span<const u64>(a1.data(), W));

    // Cross-check against a cleartext lookup. The demo asserting its own
    // correctness is worth more than the demo merely printing something.
    const auto truth = cat.Record(j);
    for (std::size_t i = 0; i < truth.size(); ++i)
      if (rec[i] != truth[i]) { ++mismatches; break; }

    std::printf("    %-4u %-6u %-22lld %s\n", r, j,
                static_cast<long long>(scores[j]), TitleFromRecord(rec).c_str());
  }

  // ---- Cross-check the ranking against the Python oracle ----------------
  //
  // export.py ranks the SAME encoded scores with the SAME mask, so for the
  // demo user the two orderings must agree item for item. This is what turns
  // the demo from a plausible-looking printout into a checked one: the titles
  // above are pleasant, but agreement with an independently computed ranking
  // is the part that would catch a regression.
  if (user == 42 && have_seen) {
    std::ifstream oracle("model/out/top10_u42.txt");
    if (oracle) {
      std::vector<std::uint32_t> want;
      std::string line;
      while (std::getline(oracle, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const std::size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        want.push_back(static_cast<std::uint32_t>(
            std::strtoul(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10)));
      }
      const std::size_t cmp = std::min(want.size(), top.size());
      std::size_t agree = 0;
      for (std::size_t i = 0; i < cmp; ++i) if (want[i] == top[i]) ++agree;
      if (agree == cmp && cmp > 0) {
        std::printf("\n    ranking agrees with model/out/top10_u42.txt on all %zu\n"
                    "    positions, in order.\n", cmp);
      } else {
        std::fprintf(stderr,
                     "\nFAIL: ranking disagrees with the Python oracle "
                     "(%zu of %zu positions match)\n", agree, cmp);
        ++mismatches;
      }
    }
  }

  std::printf("\n");
  if (mismatches == 0) {
    std::printf("All %zu records fetched privately and verified byte-exact\n"
                "against a cleartext lookup. Each fetch cost one %u-byte DPF key\n"
                "per server against a %u-entry domain.\n",
                top.size(), static_cast<unsigned>(client.Query(0).first.SizeBytes()),
                cat.DomainSize());
    return 0;
  }
  std::fprintf(stderr, "FAIL: %d fetched records did not match cleartext\n", mismatches);
  return 1;
}
