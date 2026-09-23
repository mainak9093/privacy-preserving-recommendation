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

// ---------------------------------------------------------------------------
//  D9.3: a malicious client can buy an arbitrarily large vote, and the weight
//  check takes it away again.
//
//  The honest path fixes beta = 1 inside PirClient::Query. Nothing stops a
//  client calling Gen directly with another beta, and until 2026-09-24 nothing
//  server-side looked.
// ---------------------------------------------------------------------------
void TestMaliciousHarvestWeight() {
  const std::uint32_t domain_bits = 8;
  const std::uint32_t domain = 1u << domain_bits;
  const std::uint32_t target = 42;

  auto expand = [&](u64 beta, std::vector<u64>* e0, std::vector<u64>* e1) {
    auto keys = Gen<u64>(target, beta, domain_bits);
    e0->assign(domain, 0);
    e1->assign(domain, 0);
    EvalFull<u64>(keys.first, Span<u64>(e0->data(), domain));
    EvalFull<u64>(keys.second, Span<u64>(e1->data(), domain));
  };

  // 1. The honest query carries weight exactly 1.
  std::vector<u64> h0, h1;
  expand(u64(1), &h0, &h1);
  CHECK(HarvestWeight<u64>(Span<const u64>(h0.data(), domain),
                           Span<const u64>(h1.data(), domain)) == 1);

  // 2. The attack. A million-weight vote, and every pre-existing check passes:
  //    the keys deserialise, the domain matches, the expansion is the right
  //    length, and the record still reconstructs (scaled, which the attacker
  //    simply divides out).
  const u64 kBigBeta = 1000000;
  std::vector<u64> m0, m1;
  expand(kBigBeta, &m0, &m1);

  ConsumptionAccumulator<u64> a0(domain), a1(domain);
  a0.Add(Span<const u64>(m0.data(), domain));
  a1.Add(Span<const u64>(m1.data(), domain));

  Mpc3<u64> s(4242);
  auto shared = HarvestToReplicated<u64>(s, a0.Shares(), a1.Shares());
  auto counts = OpenVec<u64>(shared);
  const std::int64_t got = static_cast<std::int64_t>(counts[target]);
  CHECK_MSG(got == static_cast<std::int64_t>(kBigBeta),
            "one malicious query should land " + std::to_string(kBigBeta) +
                " in the consumption vector, got " + std::to_string(got));
  std::printf("  D9.3: one query with beta=10^6 puts %lld in the next round's "
              "training input\n       (honest queries put 1) -- every "
              "pre-existing check passes\n",
              static_cast<long long>(got));

  // 3. The check catches it, and costs one round.
  Mpc3<u64> c(7);
  c.ResetCounters();
  const bool honest_ok =
      HarvestWeightOk<u64>(c, Span<const u64>(h0.data(), domain),
                           Span<const u64>(h1.data(), domain));
  const std::uint64_t after_one = c.Rounds();
  const bool evil_ok =
      HarvestWeightOk<u64>(c, Span<const u64>(m0.data(), domain),
                           Span<const u64>(m1.data(), domain));
  CHECK_MSG(honest_ok, "the honest query must pass the weight check");
  CHECK_MSG(!evil_ok, "the beta=10^6 query must FAIL the weight check");
  CHECK_MSG(after_one == 1, "the check must cost exactly one round per query");
  std::printf("  D9.3: the weight check accepts beta=1, rejects beta=10^6, "
              "and costs 1 round / 2 elements\n");

  // 4. The boundary. Weight is bounded; its DISTRIBUTION is not. A forged key
  //    pair summing to 1 passes, which is what a Sabre-style audit would
  //    catch and what we have NOT built. Demonstrated directly by forging the
  //    expansions rather than going through Gen.
  std::vector<u64> f0(domain, 0), f1(domain, 0);
  f0[target] = 2;            //  +2 here
  f0[target + 1] = u64(0) - 1;   //  -1 there; the two sum to 1
  const auto forged =
      HarvestWeight<u64>(Span<const u64>(f0.data(), domain),
                         Span<const u64>(f1.data(), domain));
  CHECK_MSG(forged == 1,
            "the redistribution forgery is constructed to sum to 1");
  std::printf("  D9.3: NOT closed -- a forged pair summing to 1 (+2 / -1) "
              "passes; that needs\n       the Sabre-style audit (D9.4), which "
              "is out of scope and said so\n");
}

}  // namespace

int main() {
  std::printf("test_harvest\n");
  try {
    TestHarvestRoundTrip();
    TestHarvestFeedsTraining();
    TestMaliciousHarvestWeight();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_harvest");
}
