// ==========================================================================
//  serve.cpp -- private scoring. See serve.hpp for the design and for why
//  S1 needs no multiplication protocol.
// ==========================================================================
#include "oblivrec/serve.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace oblivrec {

template <typename Ring>
void ScoreShares(const std::vector<ReplicatedShare<Ring>>& a_share,
                 Span<const Ring> B, std::uint32_t d, std::uint32_t n,
                 const std::vector<ReplicatedShare<Ring>>& mask_share,
                 std::vector<ReplicatedShare<Ring>>& out) {
  if (a_share.size() != d) {
    throw std::invalid_argument("ScoreShares: a_share must have d entries");
  }
  if (B.size() != std::size_t(d) * n) {
    throw std::invalid_argument("ScoreShares: B must be d*n entries");
  }
  if (!mask_share.empty() && mask_share.size() != n) {
    throw std::invalid_argument("ScoreShares: mask_share must be empty or n entries");
  }

  out.assign(n, ReplicatedShare<Ring>());

  // scores[j] = sum over k of a[k] * B[k][j].
  //
  // B is public, so each term is a share times a public scalar, which is
  // local. No communication, no correlated randomness, no rounds. This is
  // the whole reason NUDGE publishes B.
  //
  // Loop order is k outer, j inner so B is walked contiguously: B is
  // row-major with rows of length n.
  for (std::uint32_t k = 0; k < d; ++k) {
    const ReplicatedShare<Ring>& ak = a_share[k];
    const Ring* row = &B[std::size_t(k) * n];
    for (std::uint32_t j = 0; j < n; ++j) {
      out[j] = out[j] + ak.MulPublic(row[j]);
    }
  }

  // The mask is additive, so applying it is one more local addition.
  if (!mask_share.empty()) {
    for (std::uint32_t j = 0; j < n; ++j) {
      out[j] = out[j] + mask_share[j];
    }
  }
}

template <typename Ring>
std::vector<std::vector<ReplicatedShare<Ring>>> BuildMaskShares(
    const std::vector<std::uint8_t>& seen, std::uint32_t n) {
  if (seen.size() != n) {
    throw std::invalid_argument("BuildMaskShares: seen must have n entries");
  }
  std::vector<std::vector<ReplicatedShare<Ring>>> parties(3);
  for (auto& p : parties) p.reserve(n);

  for (std::uint32_t j = 0; j < n; ++j) {
    // Zero where unseen, the sentinel where seen. The value is shared, so no
    // server can tell which case it is from its own share.
    const Ring v = seen[j] ? static_cast<Ring>(
                                 static_cast<typename RingTraits<Ring>::Signed>(
                                     kMaskSentinel))
                           : Ring(0);
    auto s = Split<Ring>(v);
    for (int p = 0; p < 3; ++p) parties[static_cast<std::size_t>(p)].push_back(s[static_cast<std::size_t>(p)]);
  }
  return parties;
}

template <typename Ring>
void ReconstructScores(
    const std::vector<std::vector<ReplicatedShare<Ring>>>& party_shares,
    std::vector<Ring>& out) {
  if (party_shares.size() != 3) {
    throw std::invalid_argument("ReconstructScores: expected 3 parties");
  }
  const std::size_t n = party_shares[0].size();
  for (const auto& p : party_shares) {
    if (p.size() != n) {
      throw std::invalid_argument("ReconstructScores: parties disagree on length");
    }
  }
  out.assign(n, Ring(0));
  for (std::size_t j = 0; j < n; ++j) {
    out[j] = static_cast<Ring>(party_shares[0][j].lo + party_shares[1][j].lo +
                               party_shares[2][j].lo);
  }
}

std::vector<std::uint32_t> TopK(Span<const std::int64_t> scores,
                                std::uint32_t k) {
  const std::size_t n = scores.size();
  if (k > n) k = static_cast<std::uint32_t>(n);

  std::vector<std::uint32_t> idx(n);
  std::iota(idx.begin(), idx.end(), 0u);

  // Signed comparison, because scores are two's complement ring elements and
  // the mask sentinel is deeply negative. Comparing them as unsigned would
  // sort masked items FIRST, which is the opposite of the intent and would
  // be invisible in a smoke test that only looked at the top of the list.
  std::partial_sort(
      idx.begin(), idx.begin() + static_cast<std::ptrdiff_t>(k), idx.end(),
      [&](std::uint32_t x, std::uint32_t y) {
        if (scores[x] != scores[y]) return scores[x] > scores[y];
        return x < y;   // deterministic tie-break
      });
  idx.resize(k);
  return idx;
}

// --------------------------------------------------------------------------
//  Explicit instantiation, matching dpf.cpp and pir.cpp.
// --------------------------------------------------------------------------
template void ScoreShares<u64>(const std::vector<ReplicatedShare<u64>>&,
                               Span<const u64>, std::uint32_t, std::uint32_t,
                               const std::vector<ReplicatedShare<u64>>&,
                               std::vector<ReplicatedShare<u64>>&);
template void ScoreShares<u128>(const std::vector<ReplicatedShare<u128>>&,
                                Span<const u128>, std::uint32_t, std::uint32_t,
                                const std::vector<ReplicatedShare<u128>>&,
                                std::vector<ReplicatedShare<u128>>&);

template std::vector<std::vector<ReplicatedShare<u64>>> BuildMaskShares<u64>(
    const std::vector<std::uint8_t>&, std::uint32_t);
template std::vector<std::vector<ReplicatedShare<u128>>> BuildMaskShares<u128>(
    const std::vector<std::uint8_t>&, std::uint32_t);

template void ReconstructScores<u64>(
    const std::vector<std::vector<ReplicatedShare<u64>>>&, std::vector<u64>&);
template void ReconstructScores<u128>(
    const std::vector<std::vector<ReplicatedShare<u128>>>&, std::vector<u128>&);

}  // namespace oblivrec
