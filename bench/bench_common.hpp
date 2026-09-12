// ==========================================================================
//  bench_common.hpp -- the handful of helpers more than one benchmark needs.
//
//  These four were file-local statics inside bench_sweep.cpp until tasks 4.2
//  and 4.3 needed the same ratings loader, the same densifier and the same
//  network projection. Three copies of a rating parser is three chances to
//  parse differently, and the projection in particular carries a MANDATORY
//  label (see EmitProfiles) that must not drift between producers.
//
//  This header is for `bench/` only. It includes bench.hpp, which src/ is
//  forbidden to include (bench.hpp:4-5), so including this from src/ would
//  drag the benchmark harness into the library.
//
//  WHY A HEADER AND NOT A .cpp. The Makefile globs src/**/*.cpp into every
//  binary (Makefile SRC) and bench/bench_*.cpp into one binary each. A .cpp
//  here would belong to neither list and would need a Makefile rule; the
//  templates would need it to be a header anyway.
// ==========================================================================
#ifndef OBLIVREC_BENCH_COMMON_HPP
#define OBLIVREC_BENCH_COMMON_HPP

#include "oblivrec/bench.hpp"
#include "oblivrec/netprofile.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace oblivrec {
namespace bench {

// MovieLens-100K, the only dataset any of this runs on. ML-1M is a stated
// limitation, not an oversight -- see the Limitations section of the report.
constexpr std::uint32_t kFullM = 943, kFullN = 1682;

// ---- the rating list, as MovieLens ships it ----------------------------
struct Ratings {
  std::vector<std::uint64_t> u, i, r;
  std::uint64_t nnz = 0;
};

inline bool LoadRatings(const std::string& path, Ratings* out) {
  std::ifstream f(path);
  if (!f) return false;
  long u, it, rr, ts;
  while (f >> u >> it >> rr >> ts) {
    out->u.push_back(static_cast<std::uint64_t>(u));
    out->i.push_back(static_cast<std::uint64_t>(it));
    out->r.push_back(static_cast<std::uint64_t>(rr));
    ++out->nnz;
  }
  return out->nnz > 0;
}

// Build a dense m x n matrix from the rating list, keeping only the first
// `m` users. Subsampling USERS rather than ratings is what makes the m-axis
// meaningful: it is the axis NUDGE says drives the ring-width requirement.
template <typename Ring>
std::vector<Ring> Densify(const Ratings& rt, std::uint32_t m, std::uint32_t n,
                          std::uint64_t* nnz_out) {
  std::vector<Ring> U(std::size_t(m) * n, Ring(0));
  std::uint64_t nnz = 0;
  for (std::size_t k = 0; k < rt.nnz; ++k) {
    const std::uint64_t u = rt.u[k], i = rt.i[k];
    if (u >= 1 && u <= m && i >= 1 && i <= n) {
      U[std::size_t(u - 1) * n + (i - 1)] = static_cast<Ring>(rt.r[k]);
      ++nnz;
    }
  }
  *nnz_out = nnz;
  return U;
}

// ------------------------------------------------------------------------
//  The four network projections for a row that has ALREADY been measured.
//
//  Training never transmits: the substrate simulates three parties in one
//  process and counts what it would have sent. So a WAN number is the cost
//  model of netprofile.hpp applied to those counters, not a re-run. Every
//  row it writes carries `emulation: channel-level, not netem`, because
//  that is what it is and netprofile.hpp:26-35 requires it be said.
//
//  `tag_key`/`tag` is the caller's own grouping column -- "axis" for the
//  parameter sweep, "config" for the 4.2 comparison. It was hard-coded to
//  "axis" while the sweep was the only caller; a second caller would have
//  had to emit a meaningless axis field.
// ------------------------------------------------------------------------
inline void EmitProfiles(Writer& w, Row proto, std::uint64_t rounds,
                         std::uint64_t bytes, const char* tag_key,
                         const char* tag, const char* op) {
  const NetProfile profiles[] = {kProfileLocal, kProfileLan, kProfileWanA,
                                 kProfileWanB};
  for (const auto& np : profiles) {
    Row r = proto;
    r.profile = np.name;
    r.phase = "net";
    r.wall_ms = PredictedMs(rounds, bytes, np);
    r.bytes_sent = static_cast<long long>(bytes);
    r.ExtraStr("op", op);
    r.ExtraStr(tag_key, tag);
    r.Extra("rounds", static_cast<long long>(rounds));
    r.Extra("rtt_ms", np.rtt_ms);
    r.Extra("mbps", np.mbps);
    r.ExtraStr("bound_by", Bottleneck(rounds, bytes, np));
    r.ExtraStr("emulation", "channel-level, not netem");
    w.Emit(r);
  }
}

}  // namespace bench
}  // namespace oblivrec

#endif  // OBLIVREC_BENCH_COMMON_HPP
