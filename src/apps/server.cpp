// ==========================================================================
//  server.cpp -- one OblivRec server process.
//
//      server --party 0 --port 7000 [--transcript FILE]
//
//  Three of these are launched, one per party. They do not talk to each other:
//  in S1 every server-side operation is LOCAL, because B is public and
//  scores = a.B is therefore a linear map on shares. The only links are
//  client-to-server. That is a property of the protocol, not a shortcut.
//
//  Single-threaded and sequential, on purpose. A server serves one client to
//  completion, so there is nothing concurrent to express, and choosing
//  separate PROCESSES over threads (Decisions Log 2026-09-10) means no
//  threading appears anywhere in this project.
//
//  ------------------------------------------------------------------------
//  WHAT THIS PROCESS KNOWS. Only public data: the item matrix B and the
//  catalogue. Everything user-specific arrives as ONE replicated share, which
//  is information-theoretically useless on its own -- two of the three parties
//  are needed to reconstruct anything. Nothing secret is ever loaded from
//  disk here, which is why the servers can be started before any client
//  exists.
//
//  P2 holds shares and scores like the others but never sees a DPF key: the
//  DPF is (2,2) and P0/P1 were fixed as the key holders on 2026-09-08. P2
//  rejects a key rather than quietly serving it.
// ==========================================================================
#include "oblivrec/catalogue.hpp"
#include "oblivrec/channel.hpp"
#include "oblivrec/pir.hpp"
#include "oblivrec/serve.hpp"
#include "oblivrec/share.hpp"
#include "oblivrec/wire.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

bool ReadI64(const std::string& path, std::vector<std::int64_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const std::streamoff bytes = f.tellg();
  f.seekg(0, std::ios::beg);
  if (bytes <= 0 || bytes % 8 != 0) return false;
  out.resize(static_cast<std::size_t>(bytes / 8));
  for (auto& v : out) {
    std::uint8_t b[8];
    f.read(reinterpret_cast<char*>(b), 8);
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    v = static_cast<std::int64_t>(u);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  int party = -1;
  std::uint16_t port = 0;
  std::string transcript;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--party") == 0 && i + 1 < argc) {
      party = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--transcript") == 0 && i + 1 < argc) {
      transcript = argv[++i];
    } else {
      std::fprintf(stderr,
                   "usage: server --party {0,1,2} --port N [--transcript FILE]\n");
      return 2;
    }
  }
  if (party < 0 || party > 2 || port == 0) {
    std::fprintf(stderr, "server: --party {0,1,2} and --port are required\n");
    return 2;
  }

  // Public data only.
  std::vector<std::int64_t> B;
  if (!ReadI64("model/out/B.bin", B)) {
    std::fprintf(stderr,
                 "server: model/out/B.bin missing. Run: py -3.13 model/export.py\n");
    return 1;
  }
  Catalogue cat = Catalogue::LoadMovieLens("data/ml-100k/u.item");
  const std::uint32_t d = 16;
  const std::uint32_t n = cat.NumItems();
  if (B.size() != std::size_t(d) * n) {
    std::fprintf(stderr, "server: B.bin has %zu entries, expected %u\n",
                 B.size(), d * n);
    return 1;
  }
  std::vector<u64> Bring(B.size());
  for (std::size_t i = 0; i < B.size(); ++i) Bring[i] = static_cast<u64>(B[i]);

  std::unique_ptr<PirServer<u64>> pir;
  if (party == 0 || party == 1) {
    pir.reset(new PirServer<u64>(cat));
  }
  const std::size_t W = RecordWords<u64>();

  try {
    TcpListener listener(port);
    // The launcher WAITS FOR THIS LINE rather than sleeping a fixed interval,
    // which is why it is flushed explicitly and printed before accept().
    std::printf("listening on %u party=%d\n", listener.Port(), party);
    std::fflush(stdout);

    for (;;) {
      std::unique_ptr<Channel> ch = listener.Accept();
      if (!transcript.empty()) {
        ch = RecordTranscript(std::move(ch), transcript,
                              "P" + std::to_string(party));
      }

      std::vector<std::uint8_t> frame;
      bool done = false;
      while (!done) {
        try {
          ch->Recv(frame);
        } catch (const ChannelError&) {
          break;                       // client vanished; wait for the next one
        }

        switch (MsgOf(frame)) {
          case Msg::kShares: {
            std::vector<ReplicatedShare<u64>> a_share, mask_share;
            DecodeShares<u64>(frame, a_share, mask_share);
            std::vector<ReplicatedShare<u64>> out;
            ScoreShares<u64>(a_share, Span<const u64>(Bring.data(), Bring.size()),
                             d, n, mask_share, out);
            // The client reconstructs by summing the three parties' lo
            // components, so that is what goes back.
            std::vector<u64> lo(out.size());
            for (std::size_t j = 0; j < out.size(); ++j) lo[j] = out[j].lo;
            // Bind the encoded frame to a named local first. Calling
            // EncodeWords twice inside Send() would take data() from one
            // temporary and size() from another, and the first is already
            // destroyed by the time Send runs.
            const auto reply =
                EncodeWords<u64>(Msg::kScores,
                                 Span<const u64>(lo.data(), lo.size()));
            ch->Send(Span<const std::uint8_t>(reply.data(), reply.size()));
            break;
          }
          case Msg::kPirKey: {
            if (!pir) {
              std::fprintf(stderr,
                           "server P%d: refused a DPF key; the DPF is (2,2) and "
                           "P0/P1 are the key holders\n", party);
              done = true;
              break;
            }
            const DpfKey<u64> key = DpfKey<u64>::Deserialize(KeyBytesOf(frame));
            std::vector<u64> answer(W);
            pir->Answer(key, Span<u64>(answer.data(), W));
            const auto reply =
                EncodeWords<u64>(Msg::kPirAnswer,
                                 Span<const u64>(answer.data(), answer.size()));
            ch->Send(Span<const std::uint8_t>(reply.data(), reply.size()));
            break;
          }
          case Msg::kBye:
            done = true;
            break;
          default:
            std::fprintf(stderr, "server P%d: unexpected message\n", party);
            done = true;
            break;
        }
      }
      ch->Flush();
      ch->Close();
      // Loop back to accept the next client. The launcher stops us by PID, so
      // serving until killed is what lets one server instance handle both the
      // demo and the distinguisher probes without a restart in between.
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "server P%d: %s\n", party, e.what());
    return 1;
  }
  return 0;
}
