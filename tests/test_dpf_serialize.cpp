// ==========================================================================
//  test_dpf_serialize.cpp -- the wire format, and hostile input.
//
//  These checks began as a throwaway bug hunt over the code paths written on
//  day 2 but never exercised. One of them found a real bug, so the whole set
//  is kept as a regression test rather than discarded.
//
//  THE BUG IT FOUND. Deserialize accepted a party byte outside {0,1}. That is
//  not cosmetic: Eval starts with t = key.party and Traverse branches on
//  if (*t), so a party byte of 7 is truthy and the key SILENTLY evaluates as
//  party 1. A corrupted or malicious key therefore produced a wrong
//  reconstruction with no error anywhere. Keys arrive over the wire, so the
//  fix is to validate before use.
// ==========================================================================
#include "oblivrec/dpf.hpp"
#include "oblivrec/ring.hpp"
#include "oblivrec_test.hpp"

#include <string>
#include <vector>

using namespace oblivrec;

namespace {

template <typename Ring>
std::vector<std::uint8_t> Bytes(const DpfKey<Ring>& k) { return k.Serialize(); }

template <typename Ring>
DpfKey<Ring> Parse(const std::vector<std::uint8_t>& b) {
  return DpfKey<Ring>::Deserialize(
      Span<const std::uint8_t>(b.data(), b.size()));
}

// True if Deserialize rejected the input.
template <typename Ring>
bool Rejects(std::vector<std::uint8_t> b, bool also_eval = false) {
  try {
    auto k = Parse<Ring>(b);
    if (also_eval) Eval<Ring>(k, 0);
    return false;
  } catch (...) {
    return true;
  }
}

}  // namespace

int main() {
  std::printf("test_dpf_serialize\n");

  // ---- Round trip: the key must survive and evaluate identically ---------
  for (std::uint32_t db : {1u, 2u, 7u, 11u, 16u}) {
    const std::uint32_t alpha = 3u & ((1u << db) - 1u);
    auto kp = Gen<u64>(alpha, 12345ULL, db);
    for (const auto* orig : {&kp.first, &kp.second}) {
      auto back = Parse<u64>(Bytes(*orig));
      CHECK(back.party == orig->party);
      CHECK(back.domain_bits == orig->domain_bits);
      CHECK(back.cw_last == orig->cw_last);
      CHECK(back.seed == orig->seed);
      CHECK(back.SizeBytes() == orig->SizeBytes());
      bool same = true;
      const std::uint32_t lim = (db >= 12) ? 4096u : (1u << db);
      for (std::uint32_t x = 0; x < lim; ++x)
        if (Eval<u64>(back, x) != Eval<u64>(*orig, x)) same = false;
      CHECK_MSG(same, "round-tripped key evaluates differently at db=" +
                          std::to_string(db));
    }
  }

  // The serialised length must match the advertised size exactly, since the
  // report quotes SizeBytes() as the key size.
  {
    auto kp = Gen<u64>(1, 1, 11);
    CHECK(Bytes(kp.first).size() == kp.first.SizeBytes());
    auto k2 = Gen<u128>(1, 1, 11);
    CHECK(Bytes(k2.first).size() == k2.first.SizeBytes());
  }

  // ---- u128: cw_last is 16 bytes, a distinct path ------------------------
  {
    const u128 beta =
        (static_cast<u128>(0xAABBCCDDEEFF0011ULL) << 64) | 0x2233445566778899ULL;
    auto kp = Gen<u128>(9, beta, 10);
    auto back = Parse<u128>(Bytes(kp.first));
    CHECK(back.cw_last == kp.first.cw_last);
    bool same = true;
    for (std::uint32_t x = 0; x < 1024; ++x)
      if (Eval<u128>(back, x) != Eval<u128>(kp.first, x)) same = false;
    CHECK_MSG(same, "u128 round-tripped key evaluates differently");

    // The pair must still reconstruct beta after both keys survive the wire.
    auto b0 = Parse<u128>(Bytes(kp.first));
    auto b1 = Parse<u128>(Bytes(kp.second));
    const u128 got = static_cast<u128>(Eval<u128>(b0, 9) - Eval<u128>(b1, 9));
    CHECK_MSG(got == beta, "beta lost across a full serialise/deserialise pair");
  }

  // ---- Hostile input. Keys arrive over the wire. -------------------------
  {
    auto kp = Gen<u64>(1, 1, 8);
    const auto good = Bytes(kp.first);

    { auto b = good; b.resize(b.size() - 1);
      CHECK_MSG(Rejects<u64>(b), "truncated key accepted"); }

    { auto b = good; b.push_back(0);
      CHECK_MSG(Rejects<u64>(b), "over-long key accepted"); }

    { std::vector<std::uint8_t> b(20, 0);
      CHECK_MSG(Rejects<u64>(b), "key shorter than the fixed header accepted"); }

    { std::vector<std::uint8_t> b;
      CHECK_MSG(Rejects<u64>(b), "empty input accepted"); }

    // THE REGRESSION. party must be 0 or 1. Anything else is truthy and would
    // silently evaluate as party 1.
    for (std::uint8_t bad : {2, 7, 255}) {
      auto b = good; b[0] = bad;
      CHECK_MSG(Rejects<u64>(b, /*also_eval=*/true),
                "party byte " + std::to_string(bad) + " accepted");
    }

    // domain_bits is validated before it is used to size an allocation or to
    // multiply out a length.
    { auto b = good; b[1] = 0xff; b[2] = 0xff; b[3] = 0xff; b[4] = 0x7f;
      CHECK_MSG(Rejects<u64>(b), "absurd domain_bits accepted"); }
    { auto b = good; b[1] = 0; b[2] = 0; b[3] = 0; b[4] = 0;
      CHECK_MSG(Rejects<u64>(b), "domain_bits = 0 accepted"); }
    { auto b = good; b[1] = 32; b[2] = 0; b[3] = 0; b[4] = 0;
      CHECK_MSG(Rejects<u64>(b), "domain_bits = 32 accepted"); }

    // A valid party byte must of course still be accepted.
    for (std::uint8_t okp : {0, 1}) {
      auto b = good; b[0] = okp;
      CHECK_MSG(!Rejects<u64>(b), "valid party byte " + std::to_string(okp) +
                                      " was rejected");
    }
  }

  // ---- Eval and EvalFull must agree at every index ------------------------
  for (std::uint32_t db : {1u, 2u, 5u, 8u, 11u}) {
    const std::uint32_t alpha = 3u & ((1u << db) - 1u);
    auto kp = Gen<u64>(alpha, 0xdeadbeefULL, db);
    std::vector<u64> f0(std::size_t(1) << db), f1(std::size_t(1) << db);
    EvalFull<u64>(kp.first, f0);
    EvalFull<u64>(kp.second, f1);
    bool same = true;
    for (std::uint32_t x = 0; x < (1u << db); ++x)
      if (Eval<u64>(kp.first, x) != f0[x] || Eval<u64>(kp.second, x) != f1[x])
        same = false;
    CHECK_MSG(same, "Eval and EvalFull disagree at db=" + std::to_string(db));
  }

  // EvalFull must reject a wrongly sized output span rather than overrun it.
  {
    auto kp = Gen<u64>(1, 1, 6);
    std::vector<u64> wrong(10);
    bool threw = false;
    try { EvalFull<u64>(kp.first, wrong); } catch (...) { threw = true; }
    CHECK_MSG(threw, "EvalFull accepted a wrongly sized span");
  }

  // ---- u128 decimal formatting at the maximum ----------------------------
  // 39 digits into a 40-byte buffer, so there is zero margin. Worth pinning.
  {
    CHECK(RingTraits<u128>::ToDecimal(~static_cast<u128>(0)) ==
          "340282366920938463463374607431768211455");
    CHECK(RingTraits<u128>::ToDecimal(static_cast<u128>(0)) == "0");
  }

  return ::oblivrec_test::Report("test_dpf_serialize");
}
