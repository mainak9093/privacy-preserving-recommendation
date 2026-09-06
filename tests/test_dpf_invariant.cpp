// ==========================================================================
//  test_dpf_invariant.cpp -- the structural invariant, checked at EVERY node.
//
//  WHY THIS EXISTS, AND WHY IT IS WRITTEN FIRST.
//
//  A black-box test only tells you the output is wrong. A wrong control-bit
//  update typically presents as "correct at 2 bits, wrong at 8", because the
//  corruption has to accumulate before it changes a leaf. Bisecting that from
//  leaf values alone is a multi-day job.
//
//  This checker walks BOTH parties' trees in lockstep and asserts the BGI
//  structural invariant at every internal node, so a failure names the exact
//  level and the exact node. That turns a two-day bug hunt into a
//  twenty-minute one.
//
//  THE INVARIANT. Write (s_b, t_b) for party b's state at a node.
//
//    ON the path to alpha:   s_0 != s_1   and   t_0 XOR t_1 == 1
//    OFF the path:           s_0 == s_1   and   t_0 XOR t_1 == 0
//
//  Off-path the two parties hold IDENTICAL state, which is why their
//  contributions cancel. On-path they differ and their control bits disagree,
//  which is what lets the final correction word inject beta at exactly one
//  leaf. Every DPF correctness property follows from these two lines.
// ==========================================================================
#include "oblivrec/dpf.hpp"
#include "oblivrec_test.hpp"

#include <random>
#include <string>

using namespace oblivrec;

namespace {

struct NodeState {
  Block s0, s1;
  std::uint8_t t0, t1;
};

// Recursively verify the invariant at every node of the tree.
// prefix/level identify the node; on_path says whether prefix matches alpha.
void WalkAndCheck(const DpfKey<u64>& k0, const DpfKey<u64>& k1,
                  NodeState st, std::uint32_t level, std::uint32_t prefix,
                  std::uint32_t alpha, std::uint32_t domain_bits) {
  const bool on_path =
      (prefix == (alpha >> (domain_bits - level)));

  const bool seeds_equal = (st.s0 == st.s1);
  const int  t_xor = st.t0 ^ st.t1;

  std::string where = "level=" + std::to_string(level) +
                      " prefix=" + std::to_string(prefix) +
                      " on_path=" + (on_path ? "yes" : "no");

  if (on_path) {
    CHECK_MSG(!seeds_equal, where + " : seeds must DIFFER on the path to alpha");
    CHECK_MSG(t_xor == 1,   where + " : control bits must XOR to 1 on the path");
  } else {
    CHECK_MSG(seeds_equal,  where + " : seeds must be EQUAL off the path");
    CHECK_MSG(t_xor == 0,   where + " : control bits must XOR to 0 off the path");
  }

  if (level == domain_bits) return;

  for (int dir = 0; dir < 2; ++dir) {
    NodeState child = st;
    detail::Traverse(&child.s0, &child.t0, k0.cw[level], dir);
    detail::Traverse(&child.s1, &child.t1, k1.cw[level], dir);
    WalkAndCheck(k0, k1, child, level + 1, (prefix << 1) | dir, alpha,
                 domain_bits);
  }
}

void CheckOneKeyPair(std::uint32_t domain_bits, std::uint32_t alpha, u64 beta) {
  auto kp = Gen<u64>(alpha, beta, domain_bits);
  NodeState root{kp.first.seed, kp.second.seed, kp.first.party,
                 kp.second.party};
  WalkAndCheck(kp.first, kp.second, root, 0, 0, alpha, domain_bits);
}

}  // namespace

int main() {
  std::printf("test_dpf_invariant\n");

  // Exhaustive over every alpha, for domains small enough to walk fully.
  for (std::uint32_t db = 1; db <= 8; ++db) {
    const std::uint32_t n = 1u << db;
    for (std::uint32_t alpha = 0; alpha < n; ++alpha) {
      CheckOneKeyPair(db, alpha, 0x0123456789abcdefULL);
    }
    if (::oblivrec_test::Failures() != 0) {
      std::printf("  stopped at domain_bits=%u\n", db);
      break;
    }
  }

  // A few larger domains at random alpha. 2^10 nodes is still cheap to walk
  // exhaustively, and this catches level-dependent bugs the small cases miss.
  if (::oblivrec_test::Failures() == 0) {
    std::mt19937_64 rng(20260907);
    for (std::uint32_t db = 9; db <= 11; ++db) {
      for (int trial = 0; trial < 3; ++trial) {
        std::uint32_t alpha =
            static_cast<std::uint32_t>(rng() & ((1u << db) - 1));
        CheckOneKeyPair(db, alpha, static_cast<u64>(rng()));
      }
    }
  }

  // beta = 0 and alpha = 0 are the classic off-by-one traps.
  CheckOneKeyPair(6, 0, 0);
  CheckOneKeyPair(6, 63, 1);

  return ::oblivrec_test::Report("test_dpf_invariant");
}
