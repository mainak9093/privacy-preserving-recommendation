// ==========================================================================
//  probe.cpp -- generate channel-boundary transcripts for the distinguisher.
//
//      probe --alpha 0 --count 500 --port 7000 --transcript FILE
//
//  Sends `count` independent PIR queries for the SAME record index and records
//  every frame. Run it twice with two different indices and the question the
//  Phase 2 exit criterion actually asks becomes answerable:
//
//      given the bytes on the wire, can an observer tell WHICH record was
//      fetched?
//
//  Each query calls Gen afresh, so the keys are independently sampled. That
//  matters: a distinguisher fed one key repeated 500 times would be measuring
//  nothing. The variation across trials is exactly what the adversary would
//  have in practice.
//
//  This deliberately talks to ONE server. An observer positioned on one link
//  sees one key per query, and the security claim is about exactly that
//  adversary -- one who cannot see both. An observer with both links can
//  reconstruct the record by design, and no PIR scheme claims otherwise.
// ==========================================================================
#include "oblivrec/catalogue.hpp"
#include "oblivrec/channel.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/wire.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oblivrec;

int main(int argc, char** argv) {
  std::uint32_t alpha = 0, count = 100;
  std::uint16_t port = 7000;
  std::string transcript = "bench/results/distinguisher.jsonl";
  std::string host = "127.0.0.1";

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--alpha") == 0 && i + 1 < argc) {
      alpha = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
      count = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = static_cast<std::uint16_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--transcript") == 0 && i + 1 < argc) {
      transcript = argv[++i];
    } else if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      host = argv[++i];
    } else {
      std::fprintf(stderr,
                   "usage: probe --alpha N --count N [--port N] [--host H] "
                   "[--transcript FILE]\n");
      return 2;
    }
  }

  try {
    Catalogue cat = Catalogue::LoadMovieLens("data/ml-100k/u.item");
    if (alpha >= cat.DomainSize()) {
      std::fprintf(stderr, "probe: alpha %u is outside the %u-entry domain\n",
                   alpha, cat.DomainSize());
      return 2;
    }

    // The tag is what the analysis groups by: it records which index this
    // batch of frames belongs to. The ADVERSARY does not get it -- it is the
    // ground-truth label the distinguisher is scored against.
    auto ch = RecordTranscript(TcpConnect(host, port), transcript,
                              "alpha=" + std::to_string(alpha));

    PirClient<u64> client(cat.DomainBits());
    std::vector<std::uint8_t> frame;
    for (std::uint32_t i = 0; i < count; ++i) {
      auto keys = client.Query(alpha);          // fresh Gen every time
      const auto msg = EncodeKeyBytes(keys.first.Serialize());
      ch->Send(Span<const std::uint8_t>(msg.data(), msg.size()));
      ch->Recv(frame);
      if (MsgOf(frame) != Msg::kPirAnswer) {
        std::fprintf(stderr, "probe: server answered the wrong message type\n");
        return 1;
      }
    }
    const auto bye = EncodeBye();
    ch->Send(Span<const std::uint8_t>(bye.data(), bye.size()));
    ch->Flush();
    ch->Close();

    std::printf("probe: %u queries for alpha=%u recorded to %s\n", count, alpha,
                transcript.c_str());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "probe: %s\n", e.what());
    return 1;
  }
  return 0;
}
