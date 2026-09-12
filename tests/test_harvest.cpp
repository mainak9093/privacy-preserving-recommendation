// ==========================================================================
//  test_harvest.cpp -- the PIRSONA loop actually closes.
//
//  The claim: after a user fetches some items by PIR, the two servers'
//  accumulated DPF shares reconstruct to a count vector with a 1 at exactly
//  the items fetched -- with neither server able to see that from its own
//  copy, and with the result in the 2-of-3 replicated form training consumes.
//
//  If this works, the output of delivery IS the input of training, which is
//  the thing neither base paper does.
// ==========================================================================
#include "oblivrec/harvest.hpp"
#include "oblivrec_test.hpp"

#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

void TestHarvestRoundTrip() {
  const std::uint32_t domain_bits = 8;
  const std::uint32_t domain = 1u << domain_bits;

  // A catalogue is not needed to test the harvest: the accumulators are fed by
  // EvalFull, so a bare DPF exercises exactly the same path with far less
  // setup. Use the client to make real keys.
  PirClient<u64> client(domain_bits);
  ConsumptionAccumulator<u64> a0(domain), a1(domain);

  std::mt19937_64 rng(31337);
  std::map<std::uint32_t, int> want;
  const int queries = 12;
  for (int q = 0; q < queries; ++q) {
    const std::uint32_t alpha = static_cast<std::uint32_t>(rng() % domain);
    ++want[alpha];

    auto keys = client.Query(alpha);
    std::vector<u64> e0(domain), e1(domain);
    EvalFull<u64>(keys.first, Span<u64>(e0.data(), domain));
    EvalFull<u64>(keys.second, Span<u64>(e1.data(), domain));
    a0.Add(Span<const u64>(e0.data(), domain));
    a1.Add(Span<const u64>(e1.data(), domain));
  }

  CHECK(a0.Queries() == static_cast<std::uint64_t>(queries));

  // Neither accumulator alone may look like the answer. A server that could
  // read counts off its own share would make the whole exercise pointless.
  {
    auto s0 = a0.Shares();
    int looks_like_counts = 0;
    for (std::uint32_t j = 0; j < domain; ++j) {
      const std::int64_t v = static_cast<std::int64_t>(s0[j]);
      if (v >= 0 && v <= queries) ++looks_like_counts;
    }
    // With pseudorandom shares almost nothing should land in [0, queries].
    CHECK_MSG(looks_like_counts < static_cast<int>(domain) / 4,
              std::to_string(looks_like_counts) + " of " +
                  std::to_string(domain) +
                  " entries in ONE server's accumulator fall in the plausible "
                  "count range; the shares are not hiding the counts");
  }

  // Together, they are exactly the consumption vector.
  Mpc3<u64> s(9);
  s.ResetCounters();
  auto shared = HarvestToReplicated<u64>(s, a0.Shares(), a1.Shares());
  CHECK_MSG(s.Rounds() == 1, "conversion should cost exactly one round");

  auto counts = OpenVec<u64>(shared);
  int wrong = 0;
  for (std::uint32_t j = 0; j < domain; ++j) {
    const std::int64_t got = static_cast<std::int64_t>(counts[j]);
    const auto it = want.find(j);
    const std::int64_t expect = (it == want.end()) ? 0 : it->second;
    if (got != expect) ++wrong;
  }
  CHECK_MSG(wrong == 0,
            std::to_string(wrong) +
                " of the reconstructed consumption counts are wrong");
  std::printf("  harvest: %d PIR queries over %u items -> counts reconstruct "
              "exactly (%zu distinct items)\n", queries, domain, want.size());
  std::printf("           and no single server's accumulator reveals them\n");
}

// The harvested vector must be usable as training input, which means it has to
// behave like any other shared value under the substrate.
void TestHarvestFeedsTraining() {
  const std::uint32_t domain_bits = 6, domain = 1u << domain_bits;
  PirClient<u64> client(domain_bits);
  ConsumptionAccumulator<u64> a0(domain), a1(domain);

  for (std::uint32_t alpha : {3u, 3u, 7u, 20u}) {
    auto keys = client.Query(alpha);
    std::vector<u64> e0(domain), e1(domain);
    EvalFull<u64>(keys.first, Span<u64>(e0.data(), domain));
    EvalFull<u64>(keys.second, Span<u64>(e1.data(), domain));
    a0.Add(Span<const u64>(e0.data(), domain));
    a1.Add(Span<const u64>(e1.data(), domain));
  }

  Mpc3<u64> s(4);
  auto shared = HarvestToReplicated<u64>(s, a0.Shares(), a1.Shares());

  // Multiply it by itself: if the conversion produced a malformed replicated
  // sharing, the multiplication protocol gives the wrong answer even though
  // opening alone would have looked fine.
  auto sq = s.MulVec(shared, shared);
  auto got = OpenVec<u64>(sq);
  auto lin = OpenVec<u64>(shared);
  for (std::uint32_t j = 0; j < domain; ++j) {
    CHECK_MSG(got[j] == static_cast<u64>(lin[j] * lin[j]),
              "harvested shares do not survive a multiplication at index " +
                  std::to_string(j) + " -- the conversion is malformed");
  }
  CHECK(static_cast<std::int64_t>(lin[3]) == 2);
  CHECK(static_cast<std::int64_t>(lin[7]) == 1);
  CHECK(static_cast<std::int64_t>(lin[20]) == 1);
  CHECK(static_cast<std::int64_t>(lin[0]) == 0);
  std::printf("  harvest: output is a well-formed replicated sharing "
              "(survives multiplication), counts 3->2, 7->1, 20->1\n");
}

}  // namespace

int main() {
  std::printf("test_harvest\n");
  try {
    TestHarvestRoundTrip();
    TestHarvestFeedsTraining();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_harvest");
}
