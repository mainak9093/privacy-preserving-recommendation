// ==========================================================================
//  mf.cpp -- see mf.hpp, especially on why SetOrthogonal needs no
//  multiplication protocol but is still not free.
// ==========================================================================
#include "oblivrec/mf.hpp"

#include <stdexcept>

namespace oblivrec {

template <typename Ring>
ScaledVec<Ring> SetOrthogonalPublic(const ScaledVec<Ring>& v,
                                    Span<const Ring> B,
                                    std::uint32_t rows_filled, std::uint32_t n,
                                    std::uint32_t b_frac_bits) {
  if (v.v.size() != n) {
    throw std::invalid_argument("SetOrthogonalPublic: v has " +
                                std::to_string(v.v.size()) +
                                " entries, expected " + std::to_string(n));
  }
  if (B.size() < std::size_t(rows_filled) * n) {
    throw std::invalid_argument("SetOrthogonalPublic: B is too small for " +
                                std::to_string(rows_filled) + " rows");
  }

  // The projection lands two B-scales above v. Lift v to match rather than
  // truncating the projection down, because truncating here would silently
  // spend a round and defeat the deferred-truncation schedule.
  const std::uint32_t out_bits = v.frac_bits + 2 * b_frac_bits;
  if (ScaleHeadroomBits<Ring>(out_bits) <= 0) {
    throw std::overflow_error(
        "SetOrthogonalPublic: scale " + std::to_string(out_bits) +
        " leaves no magnitude bits in a " +
        std::to_string(RingTraits<Ring>::kBits) +
        "-bit ring. Truncate sooner, or widen the ring (D9.1).");
  }

  ScaledVec<Ring> out;
  out.frac_bits = out_bits;
  out.v = SharedVec<Ring>(n);

  // Lifting v is a multiply by a public power of two: local.
  const Ring lift = static_cast<Ring>(Ring(1) << (2 * b_frac_bits));
  for (int i = 0; i < 3; ++i) {
    for (std::uint32_t j = 0; j < n; ++j) {
      out.v.p[static_cast<std::size_t>(i)][j] =
          v.v.p[static_cast<std::size_t>(i)][j].MulPublic(lift);
    }
  }

  for (std::uint32_t r = 0; r < rows_filled; ++r) {
    const Ring* row = &B[std::size_t(r) * n];
    for (int i = 0; i < 3; ++i) {
      // <v, B_r>: share times public, summed. Local, and one accumulator per
      // party rather than a vector, because nothing is exchanged.
      ReplicatedShare<Ring> dot;
      for (std::uint32_t j = 0; j < n; ++j) {
        dot = dot + v.v.p[static_cast<std::size_t>(i)][j].MulPublic(row[j]);
      }
      // Subtract dot * B_r, again share times public.
      for (std::uint32_t j = 0; j < n; ++j) {
        out.v.p[static_cast<std::size_t>(i)][j] =
            out.v.p[static_cast<std::size_t>(i)][j] - dot.MulPublic(row[j]);
      }
    }
  }
  return out;
}

template <typename Ring>
SharedVec<Ring> SetOrthogonalShared(Mpc3<Ring>& s, const SharedVec<Ring>& v,
                                    const SharedMatrix<Ring>& B,
                                    std::uint32_t rows_filled) {
  if (v.size() != B.cols) {
    throw std::invalid_argument("SetOrthogonalShared: shapes disagree");
  }
  SharedVec<Ring> out = v;
  const std::uint32_t n = B.cols;

  for (std::uint32_t r = 0; r < rows_filled; ++r) {
    // Extract row r as a shared vector.
    SharedVec<Ring> row(n);
    for (int i = 0; i < 3; ++i) {
      for (std::uint32_t j = 0; j < n; ++j) {
        row.p[static_cast<std::size_t>(i)][j] =
            B.data.p[static_cast<std::size_t>(i)][std::size_t(r) * n + j];
      }
    }
    // Shared-by-shared: this is the round the public version does not pay.
    auto dot = s.InnerProduct(out, row);
    SharedVec<Ring> dot_vec(n);
    for (int i = 0; i < 3; ++i) {
      for (std::uint32_t j = 0; j < n; ++j) {
        dot_vec.p[static_cast<std::size_t>(i)][j] =
            dot.p[static_cast<std::size_t>(i)][0];
      }
    }
    auto proj = s.MulVec(dot_vec, row);
    for (int i = 0; i < 3; ++i) {
      for (std::uint32_t j = 0; j < n; ++j) {
        out.p[static_cast<std::size_t>(i)][j] =
            out.p[static_cast<std::size_t>(i)][j] -
            proj.p[static_cast<std::size_t>(i)][j];
      }
    }
  }
  return out;
}

template ScaledVec<u64> SetOrthogonalPublic<u64>(const ScaledVec<u64>&,
                                                 Span<const u64>, std::uint32_t,
                                                 std::uint32_t, std::uint32_t);
template ScaledVec<u128> SetOrthogonalPublic<u128>(const ScaledVec<u128>&,
                                                   Span<const u128>,
                                                   std::uint32_t, std::uint32_t,
                                                   std::uint32_t);
template SharedVec<u64> SetOrthogonalShared<u64>(Mpc3<u64>&,
                                                 const SharedVec<u64>&,
                                                 const SharedMatrix<u64>&,
                                                 std::uint32_t);
template SharedVec<u128> SetOrthogonalShared<u128>(Mpc3<u128>&,
                                                   const SharedVec<u128>&,
                                                   const SharedMatrix<u128>&,
                                                   std::uint32_t);

}  // namespace oblivrec
