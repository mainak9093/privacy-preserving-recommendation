// ==========================================================================
//  test_dpf_exhaustive.cpp -- the DPF correctness oracle (RULES.md C3).
//
//  The oracle is a brute-force point function: beta at alpha, zero elsewhere.
//
//  ------------------------------------------------------------------------
//  THIS IS TWO CLAIMS, NOT ONE. Conflating them is why "exhaustive to 16"
//  looked infeasible.
//
//    CLAIM 1, the headline, expressed through EvalFull:
//        EvalFull(k0)[x] - EvalFull(k1)[x] == (x == alpha ? beta : 0)
//      for every alpha and every x. This is how ARCHITECTURE section 7.1
//      states the invariant, and it is also 5 to 8 times cheaper than the
//      equivalent number of single-point evaluations, because one whole-domain
//      pass costs 2*(2^(D+1)-2) traversals against 2^D * D for the same
//      coverage via Eval.
//
//    CLAIM 2, bridging, expressed through Eval:
//        Eval(k, x) == EvalFull(k)[x]   at every index.
//      Compose the two and single-point evaluation is exhaustively correct to
//      whatever bound claim 2 reaches.
//
//  ------------------------------------------------------------------------
//  TIERS. The bound comes from argv[1], else OBLIVREC_MAX_BITS, else 11.
//
//    make test              bound 11   ~0.75 s   the default suite
//    make check             bound 8    ~0.15 s   hardened; finding UB, not coverage
//    make test-exhaustive   bound 16   ~15 min   the full REQUIREMENTS section 7 sweep
//
//  The slow tier is deliberately not in `make test`. A suite that takes
//  fifteen minutes stops being run, and then it stops catching anything.
//
//  BETA IS DERIVED PER ALPHA, not fixed. The subtlest line in Gen is the
//  (-1)^t1 factor on the final correction word, and a fixed beta would test it
//  at exactly one value per ring across billions of leaf checks.
// ==========================================================================
#include "oblivrec/dpf.hpp"
#include "oblivrec_test.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

// Deterministic beta from (db, alpha), so a failure is reproducible even
// though Gen itself draws seeds from the OS CSPRNG.
std::uint64_t SplitMix64(std::uint64_t z) {
  z += 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

template <typename Ring>
Ring BetaFor(std::uint32_t db, std::uint32_t alpha) {
  const std::uint64_t lo =
      SplitMix64((static_cast<std::uint64_t>(db) << 32) | alpha);
  if (RingTraits<Ring>::kBits <= 64) return static_cast<Ring>(lo);
  return static_cast<Ring>((static_cast<u128>(SplitMix64(lo)) << 64) | lo);
}

enum class Mode { kFullOnly, kFullAndPoint };

// Reusable buffers, sized once at the maximum bound so that the inner loops
// never allocate. A fresh vector per alpha would time the allocator.
template <typename Ring>
struct Buffers {
  std::vector<Ring> f0, f1;
  void EnsureAtLeast(std::uint32_t db) {
    const std::size_t want = std::size_t(1) << db;
    if (f0.size() < want) { f0.assign(want, Ring(0)); f1.assign(want, Ring(0)); }
  }
};

// Check one alpha across the whole domain. Returns false on first mismatch.
template <typename Ring>
bool CheckAlpha(Buffers<Ring>& buf, std::uint32_t db, std::uint32_t alpha,
                Ring beta, Mode mode) {
  auto kp = Gen<Ring>(alpha, beta, db);
  const std::size_t n = std::size_t(1) << db;
  buf.EnsureAtLeast(db);

  EvalFull<Ring>(kp.first, Span<Ring>(buf.f0.data(), n));
  EvalFull<Ring>(kp.second, Span<Ring>(buf.f1.data(), n));

  for (std::size_t x = 0; x < n; ++x) {
    // CLAIM 1: the difference invariant against the brute-force oracle.
    const Ring got = static_cast<Ring>(buf.f0[x] - buf.f1[x]);
    const Ring want = (x == alpha) ? beta : Ring(0);
    if (got != want) {
      CHECK_MSG(false,
                "difference invariant: db=" + std::to_string(db) +
                    " alpha=" + std::to_string(alpha) +
                    " x=" + std::to_string(x) +
                    " got=" + RingTraits<Ring>::ToDecimal(got) +
                    " want=" + RingTraits<Ring>::ToDecimal(want));
      return false;
    }
    // CLAIM 2: single-point evaluation agrees with the whole-domain pass.
    if (mode == Mode::kFullAndPoint) {
      const std::uint32_t xi = static_cast<std::uint32_t>(x);
      if (Eval<Ring>(kp.first, xi) != buf.f0[x] ||
          Eval<Ring>(kp.second, xi) != buf.f1[x]) {
        CHECK_MSG(false, "Eval disagrees with EvalFull: db=" +
                             std::to_string(db) + " alpha=" +
                             std::to_string(alpha) + " x=" + std::to_string(x));
        return false;
      }
    }
  }
  return true;
}

// Every alpha in the domain.
template <typename Ring>
bool SweepAllAlpha(Buffers<Ring>& buf, const char* ring, std::uint32_t db,
                   Mode mode) {
  const auto t0 = std::chrono::steady_clock::now();
  const std::uint32_t n = 1u << db;
  for (std::uint32_t alpha = 0; alpha < n; ++alpha) {
    if (!CheckAlpha<Ring>(buf, db, alpha, BetaFor<Ring>(db, alpha), mode))
      return false;
  }
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
  // Progress is printed so that a fifteen-minute run is not a blank screen,
  // and so the wall time quoted in the report is itself measured.
  std::printf("    %-4s db=%-2u every alpha x every x  %10llu leaf checks  %8.1f ms\n",
              ring, db, static_cast<unsigned long long>(n) * n, ms);
  return true;
}

// Chosen alphas: boundaries and alternating bit patterns first, because that
// is where MSB-first indexing bugs live, then seeded random fill.
std::vector<std::uint32_t> ChosenAlphas(std::uint32_t db, int count) {
  const std::uint32_t mask = (db >= 32) ? 0xffffffffu : ((1u << db) - 1u);
  std::vector<std::uint32_t> a{0u, mask, 0x55555555u & mask, 0xAAAAAAAAu & mask};
  std::mt19937_64 rng(0xC0FFEEull ^ db);
  while (static_cast<int>(a.size()) < count)
    a.push_back(static_cast<std::uint32_t>(rng() & mask));
  a.resize(count);
  return a;
}

template <typename Ring>
bool SweepChosenAlpha(Buffers<Ring>& buf, const char* ring, std::uint32_t db,
                      int count, Mode mode) {
  const auto t0 = std::chrono::steady_clock::now();
  for (std::uint32_t alpha : ChosenAlphas(db, count))
    if (!CheckAlpha<Ring>(buf, db, alpha, BetaFor<Ring>(db, alpha), mode))
      return false;
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
  std::printf("    %-4s db=%-2u %2d chosen alphas x every x            %8.1f ms\n",
              ring, db, count, ms);
  return true;
}

std::uint32_t ResolveBound(int argc, char** argv) {
  const char* src = nullptr;
  if (argc > 1) src = argv[1];
  else if (const char* e = std::getenv("OBLIVREC_MAX_BITS")) src = e;
  if (!src) return 11;                       // default tier
  const long v = std::strtol(src, nullptr, 10);
  if (v < 1 || v > 20) {
    std::printf("  bound %ld out of range 1..20\n", v);
    std::exit(2);
  }
  return static_cast<std::uint32_t>(v);
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint32_t bound = ResolveBound(argc, argv);
  std::printf("test_dpf_exhaustive (max domain_bits = %u)\n", bound);
  const auto t_start = std::chrono::steady_clock::now();

  Buffers<u64> b64;
  Buffers<u128> b128;

  // Block A. Both claims together, every alpha, small domains, both rings.
  const std::uint32_t a_hi = std::min(bound, 8u);
  for (std::uint32_t db = 1; db <= a_hi; ++db) {
    if (!SweepAllAlpha<u64>(b64, "u64", db, Mode::kFullAndPoint)) break;
    if (!SweepAllAlpha<u128>(b128, "u128", db, Mode::kFullAndPoint)) break;
  }

  // Block B. Claim 1 only, every alpha, as far as the bound allows. u128 stops
  // one bit earlier than u64 because its leaf comparisons cost twice as much.
  if (::oblivrec_test::Failures() == 0) {
    for (std::uint32_t db = 9; db <= bound; ++db) {
      if (!SweepAllAlpha<u64>(b64, "u64", db, Mode::kFullOnly)) break;
      if (db <= bound - (bound >= 12 ? 1u : 0u))
        if (!SweepAllAlpha<u128>(b128, "u128", db, Mode::kFullOnly)) break;
    }
  }

  // Block C. Above the bound, chosen alphas across the whole domain, so that
  // the default tier still touches the domains the demo and the report care
  // about without paying for every alpha.
  if (::oblivrec_test::Failures() == 0 && bound < 16) {
    const struct { std::uint32_t db; int count; } taper[] = {
        {12, 6}, {13, 4}, {14, 3}, {15, 2}, {16, 2}};
    for (const auto& s : taper) {
      if (s.db <= bound) continue;
      if (!SweepChosenAlpha<u64>(b64, "u64", s.db, s.count, Mode::kFullAndPoint))
        break;
    }
    if (::oblivrec_test::Failures() == 0)
      for (std::uint32_t db : {12u, 14u})
        if (db > bound)
          if (!SweepChosenAlpha<u128>(b128, "u128", db, 2, Mode::kFullAndPoint))
            break;
  }

  // Block D. Edge cases that off-by-one and sign errors land on. These use
  // explicit betas rather than the derived one.
  if (::oblivrec_test::Failures() == 0) {
    CHECK(CheckAlpha<u64>(b64, 1, 0, 5, Mode::kFullAndPoint));
    CHECK(CheckAlpha<u64>(b64, 1, 1, 5, Mode::kFullAndPoint));
    CHECK(CheckAlpha<u64>(b64, 8, 0, 1, Mode::kFullAndPoint));       // first index
    CHECK(CheckAlpha<u64>(b64, 8, 255, 1, Mode::kFullAndPoint));     // last index
    CHECK(CheckAlpha<u64>(b64, 8, 128, 0, Mode::kFullAndPoint));     // beta = 0
    CHECK(CheckAlpha<u64>(b64, 8, 77, ~u64(0), Mode::kFullAndPoint));// beta = -1
    CHECK(CheckAlpha<u128>(b128, 8, 3, 0, Mode::kFullAndPoint));
    CHECK(CheckAlpha<u128>(b128, 8, 3, ~static_cast<u128>(0), Mode::kFullAndPoint));

    // beta = 1 must reconstruct exactly. The PIR read layer depends on this
    // with no tolerance for an error term.
    auto kp = Gen<u64>(42, 1, 10);
    const u64 d = static_cast<u64>(Eval<u64>(kp.first, 42) -
                                   Eval<u64>(kp.second, 42));
    CHECK_MSG(d == 1, "beta=1 must reconstruct exactly, got " +
                          RingTraits<u64>::ToDecimal(d));

    // Two Gen calls on the same input must differ, or randomness is not drawn.
    auto x = Gen<u64>(7, 99, 10);
    auto y = Gen<u64>(7, 99, 10);
    CHECK(!(x.first.seed == y.first.seed));
  }

  const double total_s = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t_start).count();
  std::printf("  total %.1f s\n", total_s);
  return ::oblivrec_test::Report("test_dpf_exhaustive");
}
