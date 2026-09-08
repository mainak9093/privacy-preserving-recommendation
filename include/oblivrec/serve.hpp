// ==========================================================================
//  serve.hpp -- private scoring, the serving half of S1.
//
//  THE IDEAL FUNCTIONALITY (REQUIREMENTS 3.2):
//      scores = a . B                       for the user's embedding a
//      scores[j] = -infinity                for items the user already rated
//      T = top-k indices
//      the user gets T, the servers get nothing
//
//  ------------------------------------------------------------------------
//  WHAT S1 ACTUALLY DOES, and why it is less than it first appears.
//
//  B is PUBLIC (that is NUDGE's design and it is what makes power iteration
//  cheap), so scores = a . B is a linear map applied to a shared vector by a
//  public matrix. Each server computes its own share of the score vector
//  entirely locally, with no communication and no multiplication protocol.
//
//  The user then reconstructs the score vector and takes the top k IN THE
//  CLEAR, on their own machine. That is the corrected design recorded in
//  MEMORY.md section 8 on 2026-08-20: the servers never learn T because the
//  user selects it, not because an oblivious selector hides it. The
//  oblivious top-k apparatus was cut on 2026-09-06 for exactly this reason.
//
//  ------------------------------------------------------------------------
//  SCORES ARE 2t-SCALED, and this is load-bearing.
//
//  Both a and B are encoded at t fractional bits, so their product carries
//  2t. Truncating back to t requires Trunc_t, which is an interactive
//  protocol and belongs to S2. S1 therefore leaves scores at 2t and decodes
//  with 2t. Getting this wrong scales every score by 2^20 and the error is
//  invisible in a ranking, because it is monotone. model/export.py writes
//  its reference scores at 2t for the same reason.
//
//  ------------------------------------------------------------------------
//  THE SEEN-ITEM MASK. ARCHITECTURE 6 says the user supplies it as a share
//  so no server learns which items were rated, but never says how to
//  represent minus infinity in a finite ring. Decided here: an additive
//  shared vector carrying kMaskSentinel at seen positions and zero
//  elsewhere. Servers add it to their score shares, which is local.
//
//  Stated plainly because it matters for the report: in S1 the user
//  reconstructs the score vector anyway, so masking server-side is
//  EQUIVALENT to masking client-side. It is done server-side because that is
//  what the ideal functionality specifies and what S3 will need once the
//  servers do more than a linear map, not because S1 requires it.
// ==========================================================================
#ifndef OBLIVREC_SERVE_HPP
#define OBLIVREC_SERVE_HPP

#include <cstdint>
#include <vector>

#include "oblivrec/ring.hpp"
#include "oblivrec/share.hpp"
#include "oblivrec/span.hpp"

namespace oblivrec {

// The sentinel standing in for minus infinity.
//
// Observed encoded scores peak near 9.7e12 at 2t = 40 fractional bits, and
// the ring bottoms out near -9.2e18. -(2^55) is about -3.6e16: roughly 3700
// times below any real score, so it always sorts last, and about 256 times
// above the ring floor, so summing a whole vector of them cannot wrap.
// Both margins are checked in tests rather than asserted here.
constexpr std::int64_t kMaskSentinel = -(std::int64_t(1) << 55);

// One server's share of a user's scores over the whole catalogue.
//
// a_share:  the user's d-dimensional embedding, this server's share
// B:        the PUBLIC item embedding matrix, d rows by n columns,
//           row-major, already ring-encoded at t bits
// mask_share: this server's share of the seen-item mask, length n, or empty
//           for no masking
// out:      length n, this server's share of the scores, 2t-scaled
template <typename Ring>
void ScoreShares(const std::vector<ReplicatedShare<Ring>>& a_share,
                 Span<const Ring> B, std::uint32_t d, std::uint32_t n,
                 const std::vector<ReplicatedShare<Ring>>& mask_share,
                 std::vector<ReplicatedShare<Ring>>& out);

// Build the additive mask from the user's rated-item set. Returns three
// per-party share vectors, each of length n.
template <typename Ring>
std::vector<std::vector<ReplicatedShare<Ring>>> BuildMaskShares(
    const std::vector<std::uint8_t>& seen, std::uint32_t n);

// Reconstruct the score vector from all three parties' shares.
template <typename Ring>
void ReconstructScores(
    const std::vector<std::vector<ReplicatedShare<Ring>>>& party_shares,
    std::vector<Ring>& out);

// Top-k indices of a reconstructed score vector, taken in the clear by the
// user. Ties broken by lower index for determinism.
std::vector<std::uint32_t> TopK(Span<const std::int64_t> scores,
                                std::uint32_t k);

}  // namespace oblivrec
#endif  // OBLIVREC_SERVE_HPP
