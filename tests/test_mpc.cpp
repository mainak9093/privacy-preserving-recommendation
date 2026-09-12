// ==========================================================================
//  test_mpc.cpp -- the 3PC substrate, and the cost claim it exists to support.
//
//  Two kinds of assertion here, and the second is the interesting one.
//
//  CORRECTNESS: multiplication of shared values reconstructs to the cleartext
//  product, over random inputs, on both rings.
//
//  COST: NUDGE Thm 4.2 says communication scales with the largest intermediate
//  VECTOR, not with the input matrices. That is a claim about round and byte
//  counters, so it is asserted against them rather than repeated in prose. If
//  someone later "optimises" MatVec into a per-element exchange, correctness
//  still passes and THIS is what fails.
// ==========================================================================
#include "oblivrec/mpc.hpp"
#include "oblivrec_test.hpp"

#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

// ---- the zero shares must actually sum to zero --------------------------
template <typename Ring>
void TestZeroShares(const char* name) {
  Mpc3<Ring> s(12345);
  for (int t = 0; t < 1000; ++t) {
    const Ring a0 = s.Gen(0).Next();
    const Ring a1 = s.Gen(1).Next();
    const Ring a2 = s.Gen(2).Next();
    CHECK_MSG(static_cast<Ring>(a0 + a1 + a2) == Ring(0),
              std::string(name) + ": zero shares did not sum to zero at t=" +
                  std::to_string(t));
    // And they must not be individually zero, or they would hide nothing.
    if (t == 0) {
      CHECK_MSG(!(a0 == Ring(0) && a1 == Ring(0)),
                std::string(name) + ": zero shares are trivially zero");
    }
  }
  std::printf("  %-4s zero shares: 1000 draws, each triple sums to zero\n", name);
}

// ---- multiplication ------------------------------------------------------
template <typename Ring>
void TestMul(const char* name) {
  Mpc3<Ring> s(7);
  std::mt19937_64 rng(99);

  for (int t = 0; t < 200; ++t) {
    const std::size_t n = 1 + static_cast<std::size_t>(rng() % 8);
    std::vector<Ring> a(n), b(n);
    for (std::size_t i = 0; i < n; ++i) {
      a[i] = static_cast<Ring>(rng());
      b[i] = static_cast<Ring>(rng());
    }
    auto sa = SplitVec<Ring>(Span<const Ring>(a.data(), n));
    auto sb = SplitVec<Ring>(Span<const Ring>(b.data(), n));
    auto sz = s.MulVec(sa, sb);
    auto got = OpenVec<Ring>(sz);
    for (std::size_t i = 0; i < n; ++i) {
      CHECK_MSG(got[i] == static_cast<Ring>(a[i] * b[i]),
                std::string(name) + ": MulVec wrong at t=" + std::to_string(t));
    }
  }
  std::printf("  %-4s MulVec: 200 random vectors reconstruct to the product\n",
              name);

  // Inner product, against a cleartext accumulation in the same ring.
  for (int t = 0; t < 100; ++t) {
    const std::size_t n = 1 + static_cast<std::size_t>(rng() % 16);
    std::vector<Ring> a(n), b(n);
    Ring want = 0;
    for (std::size_t i = 0; i < n; ++i) {
      a[i] = static_cast<Ring>(rng());
      b[i] = static_cast<Ring>(rng());
      want = static_cast<Ring>(want + a[i] * b[i]);
    }
    auto sa = SplitVec<Ring>(Span<const Ring>(a.data(), n));
    auto sb = SplitVec<Ring>(Span<const Ring>(b.data(), n));
    auto got = OpenVec<Ring>(s.InnerProduct(sa, sb));
    CHECK_MSG(got.size() == 1, "InnerProduct returned the wrong length");
    CHECK_MSG(got[0] == want,
              std::string(name) + ": InnerProduct wrong at t=" +
                  std::to_string(t));
  }
  std::printf("  %-4s InnerProduct: 100 random pairs match cleartext\n", name);
}

// ---- a single party must learn nothing ----------------------------------
template <typename Ring>
void TestHiding(const char* name) {
  std::mt19937_64 rng(2026);
  int leaked = 0;
  for (int t = 0; t < 500; ++t) {
    const Ring x = static_cast<Ring>(rng());
    std::vector<Ring> one{x};
    auto sx = SplitVec<Ring>(Span<const Ring>(one.data(), 1));
    for (int i = 0; i < 3; ++i) {
      const auto& sh = sx.p[static_cast<std::size_t>(i)][0];
      if (sh.lo == x || sh.hi == x) ++leaked;
    }
  }
  CHECK_MSG(leaked == 0,
            std::string(name) + ": " + std::to_string(leaked) +
                " shares equalled the secret");
  std::printf("  %-4s hiding: no single party's share equals the secret\n", name);
}

// ---- THE COST CLAIM ------------------------------------------------------
// This is the test that would catch a well-meaning "optimisation" that keeps
// the answers right and destroys the property the design is built on.
void TestCostModel() {
  const std::uint32_t rows = 4, cols = 64;
  std::mt19937_64 rng(5);

  std::vector<u64> M(std::size_t(rows) * cols), v(cols);
  for (auto& x : M) x = rng();
  for (auto& x : v) x = rng();

  SharedMatrix<u64> sm;
  sm.rows = rows;
  sm.cols = cols;
  sm.data = SplitVec<u64>(Span<const u64>(M.data(), M.size()));
  auto sv = SplitVec<u64>(Span<const u64>(v.data(), v.size()));

  Mpc3<u64> s(1);

  // --- shared matrix x shared vector -------------------------------------
  s.ResetCounters();
  auto out = s.MatVec(sm, sv);
  const std::uint64_t mv_rounds = s.Rounds();
  const std::uint64_t mv_bytes = s.BytesSent();

  CHECK_MSG(mv_rounds == 1, "MatVec took " + std::to_string(mv_rounds) +
                                " rounds, expected exactly 1");
  // THE POINT: bytes depend on `rows`, NOT on rows*cols. With 4x64 the matrix
  // has 256 entries; if communication tracked the matrix this would be 64x
  // larger.
  const std::uint64_t expect = 3ull * rows * sizeof(u64);
  CHECK_MSG(mv_bytes == expect,
            "MatVec sent " + std::to_string(mv_bytes) + " bytes, expected " +
                std::to_string(expect) + " (= 3 parties x rows x 8). "
                "Communication must scale with the OUTPUT vector, not the matrix.");
  std::printf("  cost: MatVec %ux%u -> 1 round, %llu B "
              "(output-sized, not matrix-sized: %u entries in, %u out)\n",
              rows, cols, (unsigned long long)mv_bytes, rows * cols, rows);

  // Correctness of that same call, so the cost claim is about a real result.
  std::vector<u64> want(rows, 0);
  for (std::uint32_t r = 0; r < rows; ++r) {
    for (std::uint32_t c = 0; c < cols; ++c) {
      want[r] = static_cast<u64>(want[r] + M[std::size_t(r) * cols + c] * v[c]);
    }
  }
  auto got = OpenVec<u64>(out);
  for (std::uint32_t r = 0; r < rows; ++r) {
    CHECK_MSG(got[r] == want[r], "MatVec result wrong at row " + std::to_string(r));
  }

  // --- inner product: n products, ONE element of communication ------------
  s.ResetCounters();
  (void)s.InnerProduct(sv, sv);
  CHECK_MSG(s.Rounds() == 1, "InnerProduct should take one round");
  CHECK_MSG(s.BytesSent() == 3ull * sizeof(u64),
            "InnerProduct sent " + std::to_string(s.BytesSent()) +
                " bytes; the summation must happen BEFORE the exchange, so it "
                "is one element per party regardless of n");
  std::printf("  cost: InnerProduct over %u elements -> 1 round, %llu B\n",
              cols, (unsigned long long)s.BytesSent());

  // --- the free operations must be genuinely free ------------------------
  s.ResetCounters();
  auto pub = Mpc3<u64>::MatVecPublic(Span<const u64>(M.data(), M.size()), rows,
                                     cols, sv);
  auto pub_ip = Mpc3<u64>::InnerProductPublic(sv, Span<const u64>(v.data(), cols));
  CHECK_MSG(s.Rounds() == 0 && s.BytesSent() == 0,
            "a PUBLIC matrix or vector must cost ZERO rounds -- that is why "
            "S1's serving path needs no protocol at all");
  // And still correct.
  auto pub_got = OpenVec<u64>(pub);
  for (std::uint32_t r = 0; r < rows; ++r) {
    CHECK_MSG(pub_got[r] == want[r], "MatVecPublic disagrees with MatVec");
  }
  (void)pub_ip;
  std::printf("  cost: public matrix and public inner product -> 0 rounds, 0 B\n");
}

}  // namespace

int main() {
  std::printf("test_mpc\n");
  TestZeroShares<u64>("u64");
  TestZeroShares<u128>("u128");
  TestMul<u64>("u64");
  TestMul<u128>("u128");
  TestHiding<u64>("u64");
  TestHiding<u128>("u128");
  TestCostModel();
  return ::oblivrec_test::Report("test_mpc");
}
