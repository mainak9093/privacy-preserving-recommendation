// ==========================================================================
//  bench_dos.cpp -- task 4.7 / D9.4: the denial-of-service amplification on
//  EvalFull, measured.
//
//  docs/threat-model.md adversary class A5 says a flood of invalid keys is an
//  "unmitigated DoS on EvalFull, which is linear in the domain", and that a
//  Sabre-style logarithmic audit is the known mitigation which we have not
//  built. That is stated honestly in three places. What was never stated is
//  HOW BAD IT IS, and that is a number rather than an argument.
//
//  ------------------------------------------------------------------------
//  THE ASYMMETRY. A DPF key is O(log N) -- 21 + 18*domain_bits + 8 bytes at
//  b=64, so 227 bytes at the catalogue this project runs on. Answering it
//  requires EvalFull across the WHOLE domain, which is O(N), plus an
//  unconditional inner product over every record. That the server touches
//  every index is not an inefficiency: it is the privacy property, since any
//  data-dependent shortcut would leak which record was wanted.
//
//  So the attacker spends bytes that grow logarithmically and buys work that
//  grows linearly. The amplification is therefore N / log N and GETS WORSE as
//  the catalogue grows -- the opposite of the direction a defender wants.
//
//  ------------------------------------------------------------------------
//  WHAT IS AND IS NOT MEASURED HERE.
//
//  This measures the cost of answering a WELL-FORMED key. A malformed one is
//  cheaper for the server, because DpfKey::Deserialize rejects it before any
//  expansion -- src/dpf/dpf.cpp validates the party byte, the domain_bits
//  range and the exact length, and tests/test_dpf_serialize.cpp covers all of
//  those. So the expensive attack is the one that looks completely legitimate,
//  which is exactly why parsing hardening does not address it and an audit
//  would have to.
//
//  Absolute timings on this machine drift up to 4x between runs (thermal), so
//  read the RATIOS, which are taken within one run.
// ==========================================================================
#include "bench_common.hpp"

#include "oblivrec/bench.hpp"
#include "oblivrec/catalogue.hpp"
#include "oblivrec/dpf.hpp"
#include "oblivrec/pir.hpp"

#include <cstdio>
#include <vector>

using namespace oblivrec;
using namespace oblivrec::bench;

namespace {

constexpr int kReps = 5;

// The domains bracket the one the project actually runs (11 = 2048 items) and
// go far enough to show the trend, without pretending we ran a big catalogue.
const std::uint32_t kDomains[] = {8, 10, 11, 12, 14, 16};

}  // namespace

int main() {
  Writer w(stdout);

  std::fprintf(stderr,
               "task 4.7 / D9.4 -- what one DPF key costs the server\n");
  std::fprintf(stderr, "  %-6s %-10s %-12s %-14s %s\n", "db", "key B",
               "client ms", "server ms", "amplification");

  const std::size_t W = RecordWords<u64>();

  for (std::uint32_t db : kDomains) {
    const std::uint32_t domain = 1u << db;

    // The attacker's side: generate one key pair. Cheap by construction.
    auto keys = Gen<u64>(0, u64(1), db);
    const std::size_t key_bytes = keys.first.SizeBytes();

    Row gen_row;
    gen_row.stage = "S1";
    gen_row.phase = "pir";
    gen_row.n = static_cast<long long>(domain);
    gen_row.b = 64;
    gen_row.bytes_sent = static_cast<long long>(key_bytes);
    gen_row.ExtraStr("op", "dos_client_gen");
    gen_row.Extra("domain_bits", static_cast<long long>(db));
    gen_row.Extra("key_bytes", static_cast<long long>(key_bytes));
    const double client_ms =
        TimeAndEmit(w, gen_row, kReps,
                    [&] { auto k = Gen<u64>(0, u64(1), db); Sink(k.first); });

    // The server's side: EvalFull across the whole domain. This is the work
    // the attacker is buying.
    std::vector<u64> out(domain);
    Row ev_row;
    ev_row.stage = "S1";
    ev_row.phase = "pir";
    ev_row.n = static_cast<long long>(domain);
    ev_row.b = 64;
    ev_row.bytes_sent = 0;             // the server sends nothing back yet
    ev_row.ExtraStr("op", "dos_server_evalfull");
    ev_row.Extra("domain_bits", static_cast<long long>(db));
    ev_row.Extra("key_bytes", static_cast<long long>(key_bytes));
    const double server_ms = TimeAndEmit(
        w, ev_row, kReps, [&] {
          EvalFull<u64>(keys.first, Span<u64>(out.data(), out.size()));
          Sink(out[0]);
        }, 50.0);

    const double amp = client_ms > 0 ? server_ms / client_ms : 0.0;
    std::fprintf(stderr, "  %-6u %-10zu %-12.4f %-14.4f %.1fx\n", db,
                 key_bytes, client_ms, server_ms, amp);

    // The amplification itself, as a row rather than a thing a reader has to
    // divide out of two others.
    Row amp_row;
    amp_row.stage = "S1";
    amp_row.phase = "pir";
    amp_row.n = static_cast<long long>(domain);
    amp_row.b = 64;
    amp_row.wall_ms = server_ms;
    amp_row.bytes_sent = static_cast<long long>(key_bytes);
    amp_row.ExtraStr("op", "dos_amplification");
    amp_row.Extra("domain_bits", static_cast<long long>(db));
    amp_row.Extra("key_bytes", static_cast<long long>(key_bytes));
    amp_row.Extra("client_ms", client_ms);
    amp_row.Extra("server_ms", server_ms);
    amp_row.Extra("amplification_time", amp);
    // Ring words the server must touch per byte the attacker sent. The record
    // inner product runs over the same domain, so it is counted too.
    amp_row.Extra("server_words_touched",
                  static_cast<long long>(std::uint64_t(domain) * (1 + W)));
    amp_row.Extra("words_per_attacker_byte",
                  static_cast<double>(std::uint64_t(domain) * (1 + W)) /
                      static_cast<double>(key_bytes));
    amp_row.ExtraStr("well_formed", "true");
    w.Emit(amp_row);
  }

  std::fprintf(stderr,
               "\n  The key is O(log N) and the answer is O(N), so the "
               "amplification GROWS with\n  the catalogue. A malformed key is "
               "cheaper for the server -- Deserialize rejects\n  it before any "
               "expansion -- so the expensive attack is the one that looks "
               "legitimate.\n");
  return 0;
}
