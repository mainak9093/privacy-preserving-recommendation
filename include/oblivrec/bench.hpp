// ==========================================================================
//  bench.hpp -- JSONL benchmark row emission. Header-only.
//
//  Included ONLY by bench/*.cpp, never by anything under src/. Benchmarking
//  machinery has no business in the shipped protocol code.
//
//  ------------------------------------------------------------------------
//  THE SCHEMA is fixed by ARCHITECTURE section 10:
//
//    {git_sha, host, profile, m, n, d, ell, b, t, k, stage, phase,
//     wall_ms, cpu_ms, bytes_sent, timestamp}
//
//  Those sixteen keys are ALWAYS present, ALWAYS in that order, always with
//  the mandated meanings. A producer may append its own keys after
//  bytes_sent and before timestamp, which is what the Python oracle already
//  does with ndcg_at_20. Extras must be documented in section 10 next to the
//  producer that emits them.
//
//  CONVENTIONS worth knowing before reading a row:
//
//    wall_ms is PER OPERATION, not per batch. The schema has no iters field
//    and inventing one inside the mandated block would be worse, so the batch
//    size is carried as an `iters` extra and a reader can recover the batch
//    time by multiplying.
//
//    ONE ROW PER REPETITION, never an aggregate. RULES B5 calls the JSONL raw
//    and append-only. Aggregating inside the binary would hide the
//    distribution and force a `stat` field. Medians belong in the plot script.
//
//    git_sha, host and timestamp are filled by Emit and are never caller
//    supplied, so every row is provenanced by construction.
//
//    The timestamp format matches what the Python oracle already wrote, byte
//    for byte, so rows from both producers join without a mapping table. It
//    carries no UTC offset, which is a known limitation rather than a choice.
// ==========================================================================
#ifndef OBLIVREC_BENCH_HPP
#define OBLIVREC_BENCH_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifndef OBLIVREC_GIT_SHA
#define OBLIVREC_GIT_SHA "unknown"
#endif

namespace oblivrec {
namespace bench {

// --------------------------------------------------------------------------
//  Optimiser barriers.
//
//  "+m" rather than the more common "r,m": the register form cannot hold a
//  128-bit integer or an __m128i, both of which this project benchmarks.
// --------------------------------------------------------------------------
template <typename T>
inline void Sink(T& v) { asm volatile("" : "+m"(v) : : "memory"); }
inline void ClobberMemory() { asm volatile("" : : : "memory"); }

// --------------------------------------------------------------------------
//  The phase enum from ARCHITECTURE section 10, plus "oracle".
//
//  "oracle" names a cleartext baseline measurement. It was added because the
//  Python quality oracle already emits it and relabelling that data as
//  "matvec" would be actively false: matvec means the secret-shared phase
//  whose cost S2 is benchmarked against. Recorded in the Decisions Log.
// --------------------------------------------------------------------------
inline bool IsValidPhase(const char* p) {
  static const char* kPhases[] = {"setup", "matvec", "truncate", "normalize",
                                  "fss",   "topk",   "pir",      "net",
                                  "oracle"};
  for (const char* q : kPhases)
    if (std::strcmp(p, q) == 0) return true;
  return false;
}

// A nullable scalar. Absent becomes JSON null, matching what the oracle
// already writes for b and cpu_ms.
template <typename T>
struct Opt {
  bool has = false;
  T v{};
  Opt() = default;
  Opt(T x) : has(true), v(x) {}          // NOLINT: implicit is the point
  static Opt None() { return Opt(); }
};

class Row {
 public:
  const char* profile = "local";
  Opt<long long> m, n, d, ell, b, t, k;
  const char* stage = "S1";
  const char* phase = "fss";
  double wall_ms = 0.0;                  // per operation
  Opt<double> cpu_ms;
  long long bytes_sent = 0;

  void Extra(const char* key, long long v) {
    extras_.emplace_back(key, std::to_string(v));
  }
  void Extra(const char* key, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    extras_.emplace_back(key, buf);
  }
  void ExtraStr(const char* key, const char* v) {
    extras_.emplace_back(key, std::string("\"") + v + "\"");
  }
  void ClearExtras() { extras_.clear(); }

  const std::vector<std::pair<std::string, std::string>>& extras() const {
    return extras_;
  }

 private:
  std::vector<std::pair<std::string, std::string>> extras_;
};

class Writer {
 public:
  explicit Writer(std::FILE* out) : out_(out) {
    sha_ = OBLIVREC_GIT_SHA;
    // COMPUTERNAME is exactly the string the Python oracle wrote, so rows
    // from the two producers join on host with no mapping table and no Win32
    // call.
    const char* h = std::getenv("COMPUTERNAME");
    host_ = h ? h : "unknown";
  }

  void Emit(const Row& r) {
    if (!IsValidPhase(r.phase)) {
      // A misspelled phase silently produces a row the plot script drops, and
      // that is discovered days later. Fail here instead.
      std::fprintf(stderr, "FATAL: phase \"%s\" is not in the section 10 enum\n",
                   r.phase);
      std::abort();
    }
    if (!std::isfinite(r.wall_ms)) {
      std::fprintf(stderr, "FATAL: non-finite wall_ms would emit invalid JSON\n");
      std::abort();
    }

    std::fprintf(out_, "{");
    Str("git_sha", sha_.c_str());   Comma();
    Str("host", host_.c_str());     Comma();
    Str("profile", r.profile);      Comma();
    Int("m", r.m);    Comma();
    Int("n", r.n);    Comma();
    Int("d", r.d);    Comma();
    Int("ell", r.ell); Comma();
    Int("b", r.b);    Comma();
    Int("t", r.t);    Comma();
    Int("k", r.k);    Comma();
    Str("stage", r.stage);          Comma();
    Str("phase", r.phase);          Comma();
    Dbl("wall_ms", r.wall_ms);      Comma();
    OptDbl("cpu_ms", r.cpu_ms);     Comma();
    std::fprintf(out_, "\"bytes_sent\": %lld", r.bytes_sent);
    for (const auto& e : r.extras())
      std::fprintf(out_, ", \"%s\": %s", e.first.c_str(), e.second.c_str());
    Comma();
    Str("timestamp", Now().c_str());
    std::fprintf(out_, "}\n");
  }

 private:
  void Comma() { std::fprintf(out_, ", "); }
  void Str(const char* k, const char* v) {
    std::fprintf(out_, "\"%s\": \"%s\"", k, v);
  }
  void Dbl(const char* k, double v) {
    std::fprintf(out_, "\"%s\": %.9g", k, v);
  }
  void Int(const char* k, const Opt<long long>& o) {
    if (o.has) std::fprintf(out_, "\"%s\": %lld", k, o.v);
    else       std::fprintf(out_, "\"%s\": null", k);
  }
  void OptDbl(const char* k, const Opt<double>& o) {
    if (o.has) std::fprintf(out_, "\"%s\": %.9g", k, o.v);
    else       std::fprintf(out_, "\"%s\": null", k);
  }
  static std::string Now() {
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return buf;
  }

  std::FILE* out_;
  std::string sha_, host_;
};

class Timer {
 public:
  void Start() { t0_ = std::chrono::steady_clock::now(); }
  double WallMs() const {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0_).count();
  }
 private:
  std::chrono::steady_clock::time_point t0_{};
};

// --------------------------------------------------------------------------
//  Run fn() in batches, emitting one row per repetition.
//
//  The batch is auto-sized until it exceeds kMinBatchMs, so a 200 ns operation
//  is not timed against a clock with millisecond granularity. Each timed batch
//  is preceded by an untimed warmup, which faults pages in, forces the AES key
//  schedule's static init, and warms the branch predictor.
// --------------------------------------------------------------------------
// Returns the MEDIAN per-operation milliseconds, purely so the caller can
// print a readable table on stderr. The emitted rows remain one per
// repetition; nothing is aggregated in the data itself.
template <typename F>
double TimeAndEmit(Writer& w, Row proto, int reps, F&& fn,
                   double min_batch_ms = 100.0) {
  std::size_t iters = 1;
  Timer timer;
  for (;;) {
    timer.Start();
    for (std::size_t i = 0; i < iters; ++i) fn();
    const double ms = timer.WallMs();
    if (ms >= min_batch_ms || iters >= (std::size_t(1) << 30)) break;
    const double growth = (ms > 1e-6) ? (min_batch_ms / ms) * 1.3 : 8.0;
    std::size_t next = static_cast<std::size_t>(iters * (growth < 2.0 ? 2.0 : growth));
    iters = (next <= iters) ? iters * 2 : next;
  }

  std::vector<double> per_op;
  per_op.reserve(static_cast<std::size_t>(reps));

  for (int r = 0; r < reps; ++r) {
    for (std::size_t i = 0; i < iters; ++i) fn();      // untimed warmup
    timer.Start();
    for (std::size_t i = 0; i < iters; ++i) fn();
    const double batch_ms = timer.WallMs();
    const double ms = batch_ms / static_cast<double>(iters);
    per_op.push_back(ms);

    Row row = proto;
    row.wall_ms = ms;
    row.Extra("iters", static_cast<long long>(iters));
    row.Extra("rep", static_cast<long long>(r));
    row.Extra("batch_ms", batch_ms);
    w.Emit(row);
  }

  std::sort(per_op.begin(), per_op.end());
  return per_op.empty() ? 0.0 : per_op[per_op.size() / 2];
}

}  // namespace bench
}  // namespace oblivrec
#endif  // OBLIVREC_BENCH_HPP
