// ==========================================================================
//  factor.cpp -- ApproxFactor and the truncation schedule. See factor.hpp.
// ==========================================================================
#include "oblivrec/factor.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace oblivrec {
namespace {

// ceil(log2(x)) for a positive double, as an int. Used only on public
// parameters, so no timing concern.
int CeilLog2(double x) {
  if (x <= 1.0) return 0;
  return static_cast<int>(std::ceil(std::log2(x)));
}

}  // namespace

// --------------------------------------------------------------------------
//  Task 3.6: derive the schedule from public parameters and assert it.
// --------------------------------------------------------------------------
TruncationSchedule TruncationSchedule::Derive(const FactorParams& p,
                                              int ring_bits) {
  TruncationSchedule sch;
  sch.ring_bits_ = ring_bits;

  // Worst-case magnitude growth of one U^T (U v) step, from PUBLIC parameters
  // only, so the bound itself leaks nothing.
  //
  // The crude bound is |U^T U v| <= m * n * max_rating^2, but that assumes a
  // fully dense matrix of maximum ratings and is wildly pessimistic for real
  // data -- it costs 26 bits at ML-100K where the truth needs 22.
  //
  // Tighter, and still safe: v is unit length after every normalisation, so
  //     |U^T U v|_2 <= sigma_1(U)^2 <= ||U||_F^2 <= nnz * max_rating^2
  // and nnz is already listed as leaked by design in the threat model, so
  // using it here reveals nothing that was not already public.
  const double sq = static_cast<double>(p.max_rating) *
                    static_cast<double>(p.max_rating);
  const double growth = (p.nnz > 0)
                            ? static_cast<double>(p.nnz) * sq
                            : static_cast<double>(p.m) *
                                  static_cast<double>(p.n) * sq;
  sch.growth_ = CeilLog2(growth);

  // AFTER the two matrix products, v carries `growth` extra magnitude bits.
  // Bringing it back BEFORE anything squares it is not an optimisation, it is
  // required: ||v||^2 on an un-normalised v needs 2*(t + growth) bits, which
  // is 84 at ML-100K and overflows b=64 before a single truncation runs. This
  // was found by the subspace test failing, not by reading the algorithm.
  //
  // The shift is by a PUBLIC bound, so it is data-independent and leaks
  // nothing; it just costs one truncation per inner iteration.
  sch.post_matvec_shift_ = static_cast<std::uint32_t>(sch.growth_);

  // Peaks now: during the matrix products the scale reaches t + growth; during
  // orthogonalisation it reaches 2t (immediate) or 3t (deferred). The binding
  // constraint is whichever is larger.
  const int matvec_peak = static_cast<int>(p.t) + sch.growth_;
  const int def_peak = std::max(matvec_peak, static_cast<int>(3 * p.t));
  const int imm_peak = std::max(matvec_peak, static_cast<int>(2 * p.t));

  const int def_head = ring_bits - 1 - def_peak;
  const int imm_head = ring_bits - 1 - imm_peak;

  // Prefer the deferred schedule, but only with a real margin. Choosing it at
  // one or two spare bits would be technically inside the ring and one
  // unlucky dataset away from silent corruption.
  if (def_head >= TruncationSchedule::kMinHeadroomBits) {
    sch.deferred_ = true;
    sch.peak_frac_ = static_cast<std::uint32_t>(def_peak);
    sch.trunc_bits_ = 2 * p.t;
    sch.headroom_ = def_head;
  } else {
    sch.deferred_ = false;
    sch.peak_frac_ = static_cast<std::uint32_t>(imm_peak);
    sch.trunc_bits_ = p.t;
    sch.headroom_ = imm_head;
  }
  return sch;
}

void TruncationSchedule::AssertHeadroom() const {
  // A MARGIN, not merely a positive number. Three spare bits is technically
  // inside the ring and one unlucky dataset away from silent corruption, and
  // the growth bound is worst-case over public parameters rather than a
  // guarantee about this particular matrix. Requiring the same margin here
  // that Derive() requires to choose deferral keeps the two consistent --
  // they disagreed at first, and the test caught it.
  if (headroom_ >= kMinHeadroomBits) return;
  std::ostringstream os;
  os << "TRUNCATION SCHEDULE HAS NO HEADROOM. " << Explain()
     << "  Need at least " << kMinHeadroomBits
     << " spare bits. Fixed-point values would wrap silently and the model would be "
        "wrong without failing. Reduce t, truncate more often, or widen the "
        "ring (that is question D9.1).";
  throw std::overflow_error(os.str());
}

std::string TruncationSchedule::Explain() const {
  std::ostringstream os;
  os << "b=" << ring_bits_ << ", " << (deferred_ ? "DEFERRED" : "immediate")
     << ": 1 sign + " << peak_frac_
     << " peak scale bits (growth " << growth_ << ", post-matvec shift "
     << post_matvec_shift_ << ") = " << (1 + static_cast<int>(peak_frac_))
     << " of " << ring_bits_ << ", leaving " << headroom_ << ".";
  return os.str();
}

// --------------------------------------------------------------------------
//  Normalizers.
// --------------------------------------------------------------------------
template <typename Ring>
SharedVec<Ring> FssNormalizer<Ring>::Apply(Mpc3<Ring>&, const SharedVec<Ring>&,
                                           std::uint32_t) {
  throw std::runtime_error(
      "FssNormalizer is not implemented. ApproxNormalize needs b+1 "
      "simultaneous FSS integer comparisons (ARCHITECTURE 4.2), i.e. the "
      "comparison gate that S1 cut because its only consumers are here. The "
      "cleartext reference and its Newton-step calibration exist "
      "(ApproxNormalizeClear); the shared protocol does not. Pass "
      "RevealNormNormalizer to run today, and read what it leaks first.");
}

template <typename Ring>
SharedVec<Ring> RevealNormNormalizer<Ring>::Apply(Mpc3<Ring>& s,
                                                  const SharedVec<Ring>& v,
                                                  std::uint32_t t) {
  using S = typename RingTraits<Ring>::Signed;

  // ||v||^2 at scale 2t. One round.
  auto n2 = s.InnerProduct(v, v);

  // THE LEAK. Opening this reveals one scalar per call; across a run that is
  // the trajectory of the singular values of U. Charged as a round so it
  // appears in the cost, and recorded so it appears in the result.
  s.AccountRound(3);
  const Ring opened = static_cast<Ring>(n2.p[0][0].lo + n2.p[1][0].lo +
                                        n2.p[2][0].lo);
  const double norm2 = static_cast<double>(static_cast<S>(opened)) /
                       std::pow(2.0, static_cast<double>(2 * t));
  revealed_.push_back(norm2);

  if (!(norm2 > 0.0)) {
    // A zero vector cannot be normalised. Returning it unchanged is right:
    // SetOrthogonal can legitimately annihilate a candidate that lies in the
    // span already found, and the caller re-seeds.
    return v;
  }

  // Rescale by a PUBLIC constant, which makes the multiplication local.
  const double inv = 1.0 / std::sqrt(norm2);
  const double scaled = inv * std::pow(2.0, static_cast<double>(t));
  if (!(std::abs(scaled) < std::pow(2.0, static_cast<double>(
                                             RingTraits<Ring>::kBits - 2)))) {
    throw std::overflow_error("RevealNormNormalizer: 1/||v|| does not fit");
  }
  const Ring mul = static_cast<Ring>(static_cast<S>(std::llround(scaled)));

  SharedVec<Ring> out(v.size());
  for (int i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < v.size(); ++j) {
      out.p[static_cast<std::size_t>(i)][j] =
          v.p[static_cast<std::size_t>(i)][j].MulPublic(mul);
    }
  }
  // The product is at 2t; bring it back to t.
  return TruncatePair<Ring>(s, out, t);
}

// --------------------------------------------------------------------------
//  Orthogonalise according to the schedule.
//
//  DEFERRED   lift v to 3t, subtract the projections there, truncate once by
//             2t. One truncation, but needs 3t of fractional headroom.
//  IMMEDIATE  truncate each inner product from 2t back to t first, so the
//             projection only ever reaches 2t. Two truncations, t bits cheaper
//             -- and at b=64 it is the only one that fits.
//
//  Returns a t-scaled vector either way, so the caller does not branch.
// --------------------------------------------------------------------------
template <typename Ring>
static SharedVec<Ring> OrthogonaliseScheduled(Mpc3<Ring>& s,
                                              const SharedVec<Ring>& v,
                                              const std::vector<Ring>& B,
                                              std::uint32_t filled,
                                              const FactorParams& p,
                                              const TruncationSchedule& sched,
                                              std::uint64_t* truncations) {
  if (filled == 0) return v;   // nothing to project out, so nothing to pay

  if (sched.Deferred()) {
    ScaledVec<Ring> g{v, p.t};
    g = SetOrthogonalPublic<Ring>(g, Span<const Ring>(B.data(), B.size()),
                                  filled, p.n, p.t);
    ++(*truncations);
    return TruncatePair<Ring>(s, g.v, 2 * p.t);
  }

  // Immediate. All `filled` dot products are truncated in ONE batched call,
  // which is why the extra truncation costs rounds only once rather than once
  // per row.
  SharedVec<Ring> dots(filled);
  for (std::uint32_t r = 0; r < filled; ++r) {
    const Ring* row = &B[std::size_t(r) * p.n];
    for (int i = 0; i < 3; ++i) {
      ReplicatedShare<Ring> acc;
      for (std::uint32_t j = 0; j < p.n; ++j) {
        acc = acc + v.p[static_cast<std::size_t>(i)][j].MulPublic(row[j]);
      }
      dots.p[static_cast<std::size_t>(i)][r] = acc;
    }
  }
  auto dots_t = TruncatePair<Ring>(s, dots, p.t);   // 2t -> t
  ++(*truncations);

  // v lifted to 2t, minus dot(t) * B_r(t) which is already 2t. Local.
  SharedVec<Ring> acc2(p.n);
  const Ring lift = static_cast<Ring>(Ring(1) << p.t);
  for (int i = 0; i < 3; ++i) {
    for (std::uint32_t j = 0; j < p.n; ++j) {
      acc2.p[static_cast<std::size_t>(i)][j] =
          v.p[static_cast<std::size_t>(i)][j].MulPublic(lift);
    }
  }
  for (std::uint32_t r = 0; r < filled; ++r) {
    const Ring* row = &B[std::size_t(r) * p.n];
    for (int i = 0; i < 3; ++i) {
      const auto dr = dots_t.p[static_cast<std::size_t>(i)][r];
      for (std::uint32_t j = 0; j < p.n; ++j) {
        acc2.p[static_cast<std::size_t>(i)][j] =
            acc2.p[static_cast<std::size_t>(i)][j] - dr.MulPublic(row[j]);
      }
    }
  }
  ++(*truncations);
  return TruncatePair<Ring>(s, acc2, p.t);          // 2t -> t
}

// --------------------------------------------------------------------------
//  ApproxFactor.
// --------------------------------------------------------------------------
template <typename Ring>
FactorResult<Ring> ApproxFactorShared(Mpc3<Ring>& s, const SharedMatrix<Ring>& U,
                                      const FactorParams& p,
                                      const TruncationSchedule& sched,
                                      Normalizer<Ring>& norm,
                                      std::uint64_t seed) {
  using S = typename RingTraits<Ring>::Signed;
  sched.AssertHeadroom();          // BEFORE any work, not after it looks odd

  if (U.rows != p.m || U.cols != p.n) {
    throw std::invalid_argument("ApproxFactorShared: U shape disagrees with p");
  }

  FactorResult<Ring> res;
  res.normalizer = norm.Name();
  res.B.assign(std::size_t(p.d) * p.n, Ring(0));

  // U^T, built once. Transposing a shared matrix is a local relabelling.
  SharedMatrix<Ring> Ut;
  Ut.rows = p.n;
  Ut.cols = p.m;
  Ut.data = SharedVec<Ring>(std::size_t(p.n) * p.m);
  for (int i = 0; i < 3; ++i) {
    for (std::uint32_t r = 0; r < p.m; ++r) {
      for (std::uint32_t c = 0; c < p.n; ++c) {
        Ut.data.p[static_cast<std::size_t>(i)][std::size_t(c) * p.m + r] =
            U.data.p[static_cast<std::size_t>(i)][std::size_t(r) * p.n + c];
      }
    }
  }

  const std::uint64_t r0 = s.Rounds(), b0 = s.BytesSent();
  std::uint64_t state = seed * 6364136223846793005ull + 1442695040888963407ull;
  auto NextRand = [&state]() {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
  };

  for (std::uint32_t comp = 0; comp < p.d; ++comp) {
    // A random start, shared. Small values: the normaliser fixes the scale
    // immediately and a large start would only eat headroom.
    std::vector<Ring> v0(p.n);
    for (std::uint32_t j = 0; j < p.n; ++j) {
      const std::int64_t r = static_cast<std::int64_t>(NextRand() % 2001) - 1000;
      v0[j] = static_cast<Ring>(static_cast<S>(r));
    }
    SharedVec<Ring> v = SplitVec<Ring>(Span<const Ring>(v0.data(), p.n));

    v = OrthogonaliseScheduled<Ring>(s, v, res.B, comp, p, sched,
                                     &res.truncations);
    v = norm.Apply(s, v, p.t);

    for (std::uint32_t it = 0; it < p.ell; ++it) {
      // Two matrix products. U is at scale 0, so NEITHER needs a truncation:
      // this is what keeps the hot loop cheap.
      auto uv = s.MatVec(U, v);          // m-vector, scale t
      v = s.MatVec(Ut, uv);              // n-vector, scale t

      // Bring the magnitude back before anything squares it. See the note in
      // TruncationSchedule::Derive -- without this, ||v||^2 overflows.
      if (sched.PostMatVecShift() > 0) {
        v = TruncatePair<Ring>(s, v, sched.PostMatVecShift());
        ++res.truncations;
      }

      v = OrthogonaliseScheduled<Ring>(s, v, res.B, comp, p, sched,
                                       &res.truncations);
      v = norm.Apply(s, v, p.t);
    }

    // B[i] := v, REVEALED, exactly as section 5 specifies.
    auto opened = OpenVec<Ring>(v);
    for (std::uint32_t j = 0; j < p.n; ++j) {
      res.B[std::size_t(comp) * p.n + j] = opened[j];
    }
    s.AccountRound(3 * p.n);
  }

  res.rounds = s.Rounds() - r0;
  res.bytes = s.BytesSent() - b0;
  res.revealed_norms = norm.Revealed();
  return res;
}

// --------------------------------------------------------------------------
//  The cleartext twin, with the SAME truncation points.
// --------------------------------------------------------------------------
template <typename Ring>
std::vector<Ring> ApproxFactorClear(Span<const Ring> U, const FactorParams& p,
                                    std::uint64_t seed) {
  using S = typename RingTraits<Ring>::Signed;
  std::vector<Ring> B(std::size_t(p.d) * p.n, Ring(0));
  std::uint64_t state = seed * 6364136223846793005ull + 1442695040888963407ull;
  auto NextRand = [&state]() {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
  };

  auto Normalize = [&](std::vector<Ring>& v) {
    long double sq = 0.0L;
    for (auto x : v) {
      const long double f =
          static_cast<long double>(static_cast<S>(x)) / std::pow(2.0L, p.t);
      sq += f * f;
    }
    if (!(sq > 0.0L)) return;
    const long double inv = 1.0L / std::sqrt(sq);
    for (auto& x : v) {
      const long double f =
          static_cast<long double>(static_cast<S>(x)) / std::pow(2.0L, p.t);
      x = static_cast<Ring>(
          static_cast<S>(std::llroundl(f * inv * std::pow(2.0L, p.t))));
    }
  };

  auto Orthogonalise = [&](std::vector<Ring>& v, std::uint32_t filled) {
    for (std::uint32_t r = 0; r < filled; ++r) {
      // Accumulate in the wider signed type, then truncate by 2t -- the same
      // deferred point the shared version uses.
      long double dot = 0.0L;
      for (std::uint32_t j = 0; j < p.n; ++j) {
        dot += static_cast<long double>(static_cast<S>(v[j])) *
               static_cast<long double>(static_cast<S>(B[std::size_t(r) * p.n + j]));
      }
      dot /= std::pow(2.0L, 2.0L * p.t);
      for (std::uint32_t j = 0; j < p.n; ++j) {
        const long double bj =
            static_cast<long double>(static_cast<S>(B[std::size_t(r) * p.n + j])) /
            std::pow(2.0L, p.t);
        const long double vj =
            static_cast<long double>(static_cast<S>(v[j])) / std::pow(2.0L, p.t);
        v[j] = static_cast<Ring>(static_cast<S>(
            std::llroundl((vj - dot * bj) * std::pow(2.0L, p.t))));
      }
    }
  };

  std::vector<Ring> v(p.n), uv(p.m);
  for (std::uint32_t comp = 0; comp < p.d; ++comp) {
    for (std::uint32_t j = 0; j < p.n; ++j) {
      const std::int64_t r = static_cast<std::int64_t>(NextRand() % 2001) - 1000;
      v[j] = static_cast<Ring>(static_cast<S>(r));
    }
    Orthogonalise(v, comp);
    Normalize(v);

    for (std::uint32_t it = 0; it < p.ell; ++it) {
      for (std::uint32_t r = 0; r < p.m; ++r) {
        long double acc = 0.0L;
        for (std::uint32_t c = 0; c < p.n; ++c) {
          acc += static_cast<long double>(static_cast<S>(U[std::size_t(r) * p.n + c])) *
                 static_cast<long double>(static_cast<S>(v[c]));
        }
        uv[r] = static_cast<Ring>(static_cast<S>(std::llroundl(acc)));
      }
      for (std::uint32_t c = 0; c < p.n; ++c) {
        long double acc = 0.0L;
        for (std::uint32_t r = 0; r < p.m; ++r) {
          acc += static_cast<long double>(static_cast<S>(U[std::size_t(r) * p.n + c])) *
                 static_cast<long double>(static_cast<S>(uv[r]));
        }
        v[c] = static_cast<Ring>(static_cast<S>(std::llroundl(acc)));
      }
      Orthogonalise(v, comp);
      Normalize(v);
    }
    for (std::uint32_t j = 0; j < p.n; ++j) B[std::size_t(comp) * p.n + j] = v[j];
  }
  return B;
}

template class FssNormalizer<u64>;
template class FssNormalizer<u128>;
template class RevealNormNormalizer<u64>;
template class RevealNormNormalizer<u128>;
template FactorResult<u64> ApproxFactorShared<u64>(Mpc3<u64>&,
                                                   const SharedMatrix<u64>&,
                                                   const FactorParams&,
                                                   const TruncationSchedule&,
                                                   Normalizer<u64>&,
                                                   std::uint64_t);
template std::vector<u64> ApproxFactorClear<u64>(Span<const u64>,
                                                 const FactorParams&,
                                                 std::uint64_t);

}  // namespace oblivrec
