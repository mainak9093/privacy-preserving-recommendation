// ==========================================================================
//  test_serve.cpp -- private scoring against the Python oracle.
//
//  The headline assertion is EXACT equality, not a tolerance. Scoring is a
//  linear map over a ring: given the same encoded inputs it must produce
//  bit-identical outputs on both sides of the language boundary. A tolerance
//  here would hide precisely the endianness, sign-extension and 2t-scale
//  bugs the boundary is prone to.
//
//  Needs model/out/, which is gitignored, so it SKIPS with a clear message if
//  the export has not been run. The share arithmetic itself is tested
//  unconditionally below, so a fresh clone still exercises the logic.
// ==========================================================================
#include "oblivrec/serve.hpp"
#include "oblivrec/share.hpp"
#include "oblivrec_test.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
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
  if (bytes % 8 != 0) return false;
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

// ---- Share arithmetic, tested without needing any exported model --------
void TestShareArithmetic() {
  std::mt19937_64 rng(20260908);

  // Split then reconstruct, from all three and from each adjacent pair.
  for (int t = 0; t < 500; ++t) {
    const u64 x = rng();
    auto parts = Split<u64>(x);
    CHECK_MSG(Reconstruct<u64>(parts) == x, "3-party reconstruct failed");
    CHECK(ReconstructPair<u64>(parts[0], parts[1]) == x);
    CHECK(ReconstructPair<u64>(parts[1], parts[2]) == x);
    CHECK(ReconstructPair<u64>(parts[2], parts[0]) == x);
  }

  // A single party's pair must not equal the secret. With random shares this
  // is overwhelmingly true; a failure means Split is not actually splitting.
  {
    int leaked = 0;
    for (int t = 0; t < 500; ++t) {
      const u64 x = rng();
      auto parts = Split<u64>(x);
      for (const auto& p : parts)
        if (p.lo == x || p.hi == x) ++leaked;
    }
    CHECK_MSG(leaked == 0, std::to_string(leaked) + " shares equalled the secret");
  }

  // Linearity: shares of x plus shares of y reconstruct to x + y, and a
  // public scalar multiple reconstructs to the scaled secret. These are the
  // only two operations S1 performs on shares.
  for (int t = 0; t < 500; ++t) {
    const u64 x = rng(), y = rng(), c = rng();
    auto sx = Split<u64>(x);
    auto sy = Split<u64>(y);
    std::vector<ReplicatedShare<u64>> sum(3), scaled(3);
    for (int p = 0; p < 3; ++p) {
      sum[static_cast<std::size_t>(p)] = sx[static_cast<std::size_t>(p)] + sy[static_cast<std::size_t>(p)];
      scaled[static_cast<std::size_t>(p)] = sx[static_cast<std::size_t>(p)].MulPublic(c);
    }
    CHECK(Reconstruct<u64>(sum) == static_cast<u64>(x + y));
    CHECK(Reconstruct<u64>(scaled) == static_cast<u64>(x * c));
  }

  // Non-adjacent shares must be rejected rather than silently reconstructing
  // to nonsense.
  {
    auto parts = Split<u64>(12345);
    bool threw = false;
    try { ReconstructPair<u64>(parts[0], parts[2]); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK_MSG(threw, "ReconstructPair accepted non-adjacent parties");
  }
  std::printf("  share arithmetic: split, reconstruct, linearity all exact\n");
}

// ---- The mask sentinel must have the margins the header claims ----------
void TestMaskSentinel() {
  // Claimed: about 3700x below any real score, and about 256x above the ring
  // floor. Both checked here so the claim cannot rot.
  const std::int64_t max_score = 9700000000000LL;   // ~9.7e12 at 2t=40
  CHECK_MSG(kMaskSentinel < -max_score * 1000,
            "sentinel is not far enough below the largest real score");
  CHECK_MSG(kMaskSentinel > (std::numeric_limits<std::int64_t>::min)() / 200,
            "sentinel is too close to the ring floor to sum safely");

  // A masked item must sort below every unmasked one, including negatives.
  std::vector<std::int64_t> s = {5, -100, kMaskSentinel, 0, -9700000000000LL, 7};
  auto top = TopK(Span<const std::int64_t>(s.data(), s.size()), 6);
  CHECK_MSG(top.back() == 2,
            "masked item did not sort last; TopK may be comparing unsigned");
  std::printf("  mask sentinel: sorts last, margins hold\n");
}

}  // namespace

int main() {
  std::printf("test_serve\n");

  TestShareArithmetic();
  TestMaskSentinel();

  std::vector<std::int64_t> A, B, expect;
  const bool have_model =
      ReadI64("model/out/A.bin", A) && ReadI64("model/out/B.bin", B) &&
      ReadI64("model/out/scores_u42.bin", expect);
  if (!have_model) {
    std::printf("  SKIP model comparison: model/out/ absent (run py -3.13 model/export.py)\n");
    return ::oblivrec_test::Report("test_serve");
  }

  const std::uint32_t d = 16, n = 1682, m = 943, user = 42;
  CHECK_MSG(A.size() == std::size_t(m) * d, "A.bin has unexpected size");
  CHECK_MSG(B.size() == std::size_t(d) * n, "B.bin has unexpected size");
  CHECK_MSG(expect.size() == n, "scores_u42.bin has unexpected size");
  if (::oblivrec_test::Failures() != 0) return ::oblivrec_test::Report("test_serve");

  // The user's embedding row, split across three parties.
  std::vector<ReplicatedShare<u64>> a_share(d);
  {
    std::vector<std::vector<ReplicatedShare<u64>>> per_party(3);
    for (auto& p : per_party) p.resize(d);
    for (std::uint32_t k = 0; k < d; ++k) {
      const u64 v = static_cast<u64>(A[std::size_t(user) * d + k]);
      auto s = Split<u64>(v);
      for (int p = 0; p < 3; ++p) per_party[static_cast<std::size_t>(p)][k] = s[static_cast<std::size_t>(p)];
    }

    // Each server scores locally against the public B.
    std::vector<u64> Bring(B.size());
    for (std::size_t i = 0; i < B.size(); ++i) Bring[i] = static_cast<u64>(B[i]);

    std::vector<std::vector<ReplicatedShare<u64>>> score_shares(3);
    for (int p = 0; p < 3; ++p) {
      ScoreShares<u64>(per_party[static_cast<std::size_t>(p)],
                       Span<const u64>(Bring.data(), Bring.size()), d, n,
                       {}, score_shares[static_cast<std::size_t>(p)]);
    }

    std::vector<u64> got;
    ReconstructScores<u64>(score_shares, got);
    CHECK_MSG(got.size() == n, "reconstructed score vector has the wrong length");

    // EXACT equality against Python. A linear map over a ring has no
    // tolerance to spend.
    int wrong = 0;
    std::int64_t worst = 0;
    for (std::uint32_t j = 0; j < n && j < got.size(); ++j) {
      const std::int64_t g = static_cast<std::int64_t>(got[j]);
      if (g != expect[j]) {
        ++wrong;
        const std::int64_t diff = g - expect[j];
        if (diff > worst || -diff > worst) worst = diff > 0 ? diff : -diff;
      }
    }
    CHECK_MSG(wrong == 0,
              std::to_string(wrong) + " of " + std::to_string(n) +
                  " scores differ from the Python oracle, worst by " +
                  std::to_string(worst));
    if (wrong == 0) {
      std::printf("  scored user %u: all %u scores EXACTLY match the oracle\n",
                  user, n);
    }
    (void)a_share;
  }

  // With a mask applied, seen items must fall to the bottom of the ranking.
  {
    std::vector<std::uint8_t> seen(n, 0);
    for (std::uint32_t j = 0; j < 50; ++j) seen[j] = 1;   // pretend 0..49 rated

    std::vector<std::vector<ReplicatedShare<u64>>> per_party(3);
    for (auto& p : per_party) p.resize(d);
    for (std::uint32_t k = 0; k < d; ++k) {
      auto s = Split<u64>(static_cast<u64>(A[std::size_t(user) * d + k]));
      for (int p = 0; p < 3; ++p) per_party[static_cast<std::size_t>(p)][k] = s[static_cast<std::size_t>(p)];
    }
    auto mask = BuildMaskShares<u64>(seen, n);

    std::vector<u64> Bring(B.size());
    for (std::size_t i = 0; i < B.size(); ++i) Bring[i] = static_cast<u64>(B[i]);

    std::vector<std::vector<ReplicatedShare<u64>>> score_shares(3);
    for (int p = 0; p < 3; ++p) {
      ScoreShares<u64>(per_party[static_cast<std::size_t>(p)],
                       Span<const u64>(Bring.data(), Bring.size()), d, n,
                       mask[static_cast<std::size_t>(p)],
                       score_shares[static_cast<std::size_t>(p)]);
    }
    std::vector<u64> got;
    ReconstructScores<u64>(score_shares, got);

    std::vector<std::int64_t> signed_scores(n);
    for (std::uint32_t j = 0; j < n; ++j)
      signed_scores[j] = static_cast<std::int64_t>(got[j]);

    auto top = TopK(Span<const std::int64_t>(signed_scores.data(), n), 20);
    int masked_in_top = 0;
    for (std::uint32_t j : top) if (j < 50) ++masked_in_top;
    CHECK_MSG(masked_in_top == 0,
              std::to_string(masked_in_top) +
                  " masked items appeared in the top 20");
    std::printf("  masking: none of the 50 seen items reach the top 20\n");
  }

  return ::oblivrec_test::Report("test_serve");
}
