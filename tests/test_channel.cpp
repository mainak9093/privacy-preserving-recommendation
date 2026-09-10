// ==========================================================================
//  test_channel.cpp -- framing, and the two ways framing goes wrong.
//
//  Binds its own listener on port 0 ("pick one"), so this never collides with
//  a running server and needs nothing started beforehand.
//
//  The split-delivery test is the point of this file. Everything else here
//  would pass with a naive one-send-equals-one-recv implementation; that test
//  is the one that fails.
// ==========================================================================
#include "oblivrec/channel.hpp"
#include "oblivrec_test.hpp"

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace oblivrec;

namespace {

// ---- Length codec, no socket needed -------------------------------------
void TestLengthCodec() {
  const std::uint32_t cases[] = {0u, 1u, 227u, 255u, 256u, 65535u, 65536u,
                                 kMaxFrameBytes, 0x01020304u, 0xFFFFFFFFu};
  for (std::uint32_t v : cases) {
    std::uint8_t b[4];
    EncodeLength(v, b);
    CHECK_MSG(DecodeLength(b) == v,
              "length codec round-trip failed for " + std::to_string(v));
  }

  // Little-endian with explicit shifts, asserted against a literal so a
  // host-order regression cannot pass.
  std::uint8_t b[4];
  EncodeLength(0x01020304u, b);
  CHECK_MSG(b[0] == 0x04 && b[1] == 0x03 && b[2] == 0x02 && b[3] == 0x01,
            "length prefix is not little-endian");

  // The cap is enforced, and enforced BEFORE any allocation.
  bool threw = false;
  try { CheckFrameLength(kMaxFrameBytes + 1); }
  catch (const ChannelError&) { threw = true; }
  CHECK_MSG(threw, "an over-cap frame length was accepted");

  threw = false;
  try { CheckFrameLength(0xFFFFFFFFu); }
  catch (const ChannelError&) { threw = true; }
  CHECK_MSG(threw, "a 4 GB frame length was accepted");

  std::printf("  length codec: little-endian, round-trips, cap enforced\n");
}

// ---- Base64, checked rather than trusted --------------------------------
void TestBase64() {
  std::mt19937_64 rng(20260911);
  for (int t = 0; t < 200; ++t) {
    const std::size_t n = static_cast<std::size_t>(rng() % 300);
    std::vector<std::uint8_t> data(n);
    for (auto& b : data) b = static_cast<std::uint8_t>(rng() & 0xFF);
    const std::string enc =
        Base64Encode(Span<const std::uint8_t>(data.data(), data.size()));
    const std::vector<std::uint8_t> dec = Base64Decode(enc);
    CHECK_MSG(dec == data, "base64 round-trip failed at n=" + std::to_string(n));
  }
  // Known-answer, so a symmetric bug in both directions cannot hide.
  const std::uint8_t abc[] = {'M', 'a', 'n'};
  CHECK(Base64Encode(Span<const std::uint8_t>(abc, 3)) == "TWFu");
  const std::uint8_t ab[] = {'M', 'a'};
  CHECK(Base64Encode(Span<const std::uint8_t>(ab, 2)) == "TWE=");
  const std::uint8_t a[] = {'M'};
  CHECK(Base64Encode(Span<const std::uint8_t>(a, 1)) == "TQ==");
  std::printf("  base64: 200 random round-trips + RFC known answers\n");
}

// ---- Loopback framing ---------------------------------------------------
// One process, two sockets. The listener accepts, and the two ends talk.
void TestLoopbackFraming() {
  TcpListener listener(0);                  // 0 = let the OS choose
  const std::uint16_t port = listener.Port();
  CHECK_MSG(port != 0, "listener did not report a bound port");

  auto client = TcpConnect("127.0.0.1", port);
  auto server = listener.Accept();

  std::mt19937_64 rng(4242);
  std::vector<std::uint8_t> out;

  // Sizes chosen to cross the interesting boundaries: empty, one byte, the
  // real DPF key size, and a score-share vector at MovieLens scale.
  for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(227),
                        std::size_t(1682 * 8)}) {
    std::vector<std::uint8_t> payload(n);
    for (auto& b : payload) b = static_cast<std::uint8_t>(rng() & 0xFF);

    client->Send(Span<const std::uint8_t>(payload.data(), payload.size()));
    server->Recv(out);
    CHECK_MSG(out == payload,
              "frame of " + std::to_string(n) + " bytes did not round-trip");
  }

  // Several frames in flight before any is read: the receiver must not
  // coalesce them into one.
  std::vector<std::vector<std::uint8_t>> sent;
  for (int i = 0; i < 5; ++i) {
    std::vector<std::uint8_t> p(static_cast<std::size_t>(10 + i * 37));
    for (auto& b : p) b = static_cast<std::uint8_t>(i);
    client->Send(Span<const std::uint8_t>(p.data(), p.size()));
    sent.push_back(p);
  }
  for (int i = 0; i < 5; ++i) {
    server->Recv(out);
    CHECK_MSG(out == sent[static_cast<std::size_t>(i)],
              "pipelined frame " + std::to_string(i) + " was mis-framed");
  }

  // Byte accounting includes the 4-byte prefixes, which is what the
  // benchmarks report.
  CHECK_MSG(client->BytesSent() > 0, "BytesSent stayed zero");
  CHECK_MSG(server->BytesReceived() == client->BytesSent(),
            "receiver and sender disagree on the byte count");

  std::printf("  loopback: 4 sizes round-trip, 5 pipelined frames stay framed\n");
}

// ---- THE ONE THAT MATTERS ----------------------------------------------
// TCP is a stream. A frame split across many reads must still be received as
// ONE frame. A naive implementation passes every test above and fails this.
//
// SIZING THIS TEST IS ITSELF A TRAP, and the first version deadlocked on it.
// With a single thread and blocking sockets, the sender and receiver are the
// same thread: if the payload exceeds what the kernel will buffer, send()
// blocks waiting for a reader that cannot run until send() returns, and the
// test hangs forever rather than failing.
//
// So the split is forced from the OTHER end instead. The listener is given a
// deliberately tiny receive buffer, which accepted sockets inherit, and the
// payload is kept comfortably inside the default SEND buffer. send() therefore
// returns immediately, and the server is obliged to reassemble the frame from
// many small reads. Deterministic, and it cannot deadlock.
void TestSplitDelivery() {
  TcpListener listener(0, 2048);            // 2 KB receive buffer, on purpose
  auto client = TcpConnect("127.0.0.1", listener.Port());
  auto server = listener.Accept();

  const std::size_t n = 16384;              // >> 2 KB rcvbuf, << 64 KB sndbuf
  std::vector<std::uint8_t> payload(n);
  for (std::size_t i = 0; i < n; ++i) {
    payload[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
  }
  client->Send(Span<const std::uint8_t>(payload.data(), payload.size()));

  std::vector<std::uint8_t> out;
  server->Recv(out);
  CHECK_MSG(out.size() == n,
            "split frame reassembled to " + std::to_string(out.size()) +
                " bytes, expected " + std::to_string(n));
  CHECK_MSG(out == payload, "split frame reassembled with wrong content");
  std::printf("  split delivery: a %zu B frame over a 2 KB receive buffer "
              "arrives as ONE frame\n", n);
}

// ---- A peer that closes mid-frame is an error, not a hang ---------------
void TestCloseMidStream() {
  TcpListener listener(0);
  auto client = TcpConnect("127.0.0.1", listener.Port());
  auto server = listener.Accept();

  client->Close();

  bool threw = false;
  std::vector<std::uint8_t> out;
  try { server->Recv(out); }
  catch (const ChannelError&) { threw = true; }
  CHECK_MSG(threw, "reading from a closed peer did not raise");
  std::printf("  closed peer: reported as an error rather than hanging\n");
}

// ---- Oversized send is refused locally ----------------------------------
void TestOversizedSend() {
  TcpListener listener(0);
  auto client = TcpConnect("127.0.0.1", listener.Port());
  auto server = listener.Accept();
  (void)server;

  std::vector<std::uint8_t> huge(kMaxFrameBytes + 1, 0);
  bool threw = false;
  try {
    client->Send(Span<const std::uint8_t>(huge.data(), huge.size()));
  } catch (const ChannelError&) { threw = true; }
  CHECK_MSG(threw, "an over-cap frame was sent instead of refused");
  std::printf("  oversize: refused before it reaches the socket\n");
}

}  // namespace

int main() {
  std::printf("test_channel\n");
  try {
    TestLengthCodec();
    TestBase64();
    TestLoopbackFraming();
    TestSplitDelivery();
    TestCloseMidStream();
    TestOversizedSend();
  } catch (const std::exception& e) {
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }
  return ::oblivrec_test::Report("test_channel");
}
