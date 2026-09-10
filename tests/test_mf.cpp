// ==========================================================================
//  test_mf.cpp -- SetOrthogonal, and the MatVecProgram round-count contract.
//
//  The claim under test is NOT "orthogonalisation is fast". It is the precise
//  one from mf.hpp: with B's rows public, SetOrthogonal spends no rounds on
//  ARITHMETIC, because every product is share-times-public. The identical
//  computation with B shared is measured alongside, so the difference is a
//  number rather than an assertion.
//
//  Correctness is checked in the ring against a cleartext Gram-Schmidt at the
//  same fixed-point scales -- exact equality, since nothing here approximates.
// ==========================================================================
#include "oblivrec/mf.hpp"
#include "oblivrec/mvp.hpp"
#include "oblivrec_test.hpp"

#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

constexpr std::uint32_t kT = 20;   // fractional bits, as everywhere else

// ---- SetOrthogonal against a public B is LOCAL --------------------------
void TestSetOrthogonalIsLocal() {
  const std::uint32_t n = 24, rows = 3;
  std::mt19937_64 rng(1234);

  // Small magnitudes, so the 3t scale stays inside u64's headroom. This is
  // itself the point mf.hpp makes about b=64 at t=20 leaving 4 bits.
  std::vector<u64> B(std::size_t(rows) * n), v(n);
  for (auto& x : B) x = static_cast<u64>(rng() % 8);
  for (auto& x : v) x = static_cast<u64>(rng() % 8);

  ScaledVec<u64> sv;
  sv.v = SplitVec<u64>(Span<const u64>(v.data(), n));
  sv.frac_bits = kT;

  Mpc3<u64> s(3);
  s.ResetCounters();
  auto out = SetOrthogonalPublic<u64>(sv, Span<const u64>(B.data(), B.size()),
                                      rows, n, kT);
  CHECK_MSG(s.Rounds() == 0 && s.BytesSent() == 0,
            "SetOrthogonalPublic moved the counters; with B public every "
            "product is share-times-public and must be local");

  // Scale bookkeeping must be explicit and right.
  CHECK_MSG(out.frac_bits == kT + 2 * kT,
            "result scale is " + std::to_string(out.frac_bits) +
                ", expected " + std::to_string(3 * kT));

  // Cleartext Gram-Schmidt in the same ring, at the same scales.
  std::vector<u64> want(n);
  const u64 lift = static_cast<u64>(u64(1) << (2 * kT));
  for (std::uint32_t j = 0; j < n; ++j) want[j] = static_cast<u64>(v[j] * lift);
  for (std::uint32_t r = 0; r < rows; ++r) {
    u64 dot = 0;
    for (std::uint32_t j = 0; j < n; ++j) {
      dot = static_cast<u64>(dot + v[j] * B[std::size_t(r) * n + j]);
    }
    for (std::uint32_t j = 0; j < n; ++j) {
      want[j] = static_cast<u64>(want[j] - dot * B[std::size_t(r) * n + j]);
    }
  }

  auto got = OpenVec<u64>(out.v);
  for (std::uint32_t j = 0; j < n; ++j) {
    CHECK_MSG(got[j] == want[j],
              "SetOrthogonalPublic disagrees with cleartext at " +
                  std::to_string(j));
  }
  std::printf("  SetOrthogonal (public B): %u rows, %u cols -> 0 rounds, 0 B, "
              "exact\n", rows, n);
}

// ---- the same thing with B shared, to price the difference --------------
void TestSetOrthogonalSharedCost() {
  const std::uint32_t n = 24, rows = 3;
  std::mt19937_64 rng(99);
  std::vector<u64> B(std::size_t(rows) * n), v(n);
  for (auto& x : B) x = static_cast<u64>(rng() % 8);
  for (auto& x : v) x = static_cast<u64>(rng() % 8);

  SharedMatrix<u64> sb;
  sb.rows = rows;
  sb.cols = n;
  sb.data = SplitVec<u64>(Span<const u64>(B.data(), B.size()));
  auto sv = SplitVec<u64>(Span<const u64>(v.data(), n));

  Mpc3<u64> s(4);
  s.ResetCounters();
  (void)SetOrthogonalShared<u64>(s, sv, sb, rows);

  // Two rounds per row: the inner product, then the scaled subtraction.
  CHECK_MSG(s.Rounds() == 2ull * rows,
            "SetOrthogonalShared took " + std::to_string(s.Rounds()) +
                " rounds, expected " + std::to_string(2 * rows));
  std::printf("  SetOrthogonal (shared B): same result costs %llu rounds, "
              "%llu B -- the price of NOT opening B\n",
              (unsigned long long)s.Rounds(),
              (unsigned long long)s.BytesSent());
}

// ---- MatVecProgram: declared rounds must equal measured rounds ----------
void TestProgramRoundContract() {
  const std::uint32_t n = 8;
  std::mt19937_64 rng(7);
  std::vector<u64> M(std::size_t(n) * n), v(n);
  for (auto& x : M) x = static_cast<u64>(rng() % 4);
  for (auto& x : v) x = static_cast<u64>(rng() % 4);

  SharedMatrix<u64> sm;
  sm.rows = n;
  sm.cols = n;
  sm.data = SplitVec<u64>(Span<const u64>(M.data(), M.size()));
  auto sv = SplitVec<u64>(Span<const u64>(v.data(), n));

  // Two shared stages and one public stage. The draft's "rounds == number of
  // NonLinear stages" would predict 0 here; the truth is 2, one per SHARED
  // matrix. See the correction at the top of mvp.hpp.
  MatVecProgram<u64> prog;
  prog.Push(sm, std::unique_ptr<NonLinear<u64>>(new NoOp<u64>()));
  prog.PushPublic(M, n, n, std::unique_ptr<NonLinear<u64>>(new NoOp<u64>()));
  prog.Push(sm, std::unique_ptr<NonLinear<u64>>(new NoOp<u64>()));

  Mpc3<u64> s(11);
  s.ResetCounters();
  (void)prog.Run(s, sv);

  CHECK_MSG(prog.DeclaredRounds() == 2,
            "declared rounds = " + std::to_string(prog.DeclaredRounds()) +
                ", expected 2 (one per SHARED matrix; the public one is free)");
  CHECK_MSG(s.Rounds() == prog.DeclaredRounds(),
            "measured " + std::to_string(s.Rounds()) + " rounds but declared " +
                std::to_string(prog.DeclaredRounds()) +
                " -- the cost model and the code disagree");
  std::printf("  MatVecProgram: %s\n", prog.Describe().c_str());
  std::printf("  MatVecProgram: declared %u rounds, measured %llu -- agree\n",
              prog.DeclaredRounds(), (unsigned long long)s.Rounds());
}

}  // namespace

int main() {
  std::printf("test_mf\n");
  try {
    TestSetOrthogonalIsLocal();
    TestSetOrthogonalSharedCost();
    TestProgramRoundContract();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_mf");
}
