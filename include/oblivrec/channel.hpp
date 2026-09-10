// ==========================================================================
//  channel.hpp -- length-prefixed framing over a byte stream.
//
//  REQUIREMENTS section 5 fixes the transport: "plain TCP, length-prefixed
//  frames. No hidden costs to explain away in benchmarks." ARCHITECTURE
//  section 8 gives src/net/ the job of "framing, batching, flush()".
//
//  ------------------------------------------------------------------------
//  WHY A FRAMING LAYER EXISTS AT ALL.
//
//  TCP is a byte STREAM, not a message service. A 227-byte DPF key handed to
//  send() may arrive as 200 bytes then 27, or two keys may arrive coalesced in
//  one read. Anything that assumes one send() equals one recv() works on
//  loopback with small payloads and then fails on a real link -- which is the
//  worst kind of bug, because the demo passes on the dev box.
//
//  So a frame is: 4-byte little-endian length, then exactly that many bytes.
//  Recv loops until the whole frame is in hand. tests/test_channel.cpp forces
//  a split delivery deliberately rather than hoping to observe one.
//
//  Little-endian with EXPLICIT SHIFTS, never a memcpy of a uint32_t, matching
//  what RingTraits::ToBytes already does. The wire format must not depend on
//  the host's byte order.
//
//  ------------------------------------------------------------------------
//  THE LENGTH PREFIX IS UNTRUSTED INPUT.
//
//  It arrives over a socket, so a peer -- or anything that can reach the port
//  -- chooses it. Reading it and then allocating that many bytes is a remote
//  memory-exhaustion bug: four bytes saying 4 GB is a four-byte denial of
//  service. It is validated against kMaxFrameBytes BEFORE it sizes anything.
//
//  This is the same discipline DpfKey::Deserialize already applies to
//  domain_bits, for the same reason, and it makes this the third piece of code
//  in the project that parses attacker-shaped input. The other two are covered
//  by `make check`; so is this one.
//
//  ------------------------------------------------------------------------
//  ON batching AND flush(). ARCHITECTURE section 8 names both. Flush() is here
//  and is honest -- with blocking sockets and no user-space buffer there is
//  nothing queued, so it is a no-op documented as one. Batching is NOT built:
//  S1 exchanges a handful of frames per query, so there is nothing to batch
//  yet. An unused optimisation would be harder to justify than its absence.
// ==========================================================================
#ifndef OBLIVREC_CHANNEL_HPP
#define OBLIVREC_CHANNEL_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "oblivrec/span.hpp"

namespace oblivrec {

// Frames larger than this are rejected without allocating. The largest thing
// S1 sends is a score-share vector: n ring words, 1682 * 8 = 13,456 bytes at
// MovieLens scale. 1 MiB leaves two orders of magnitude of headroom while
// still bounding what a hostile peer can make us allocate.
constexpr std::uint32_t kMaxFrameBytes = 1u << 20;

// Thrown for every transport and framing failure. A caller that wants to
// distinguish "peer closed cleanly" from "peer sent nonsense" reads what().
class ChannelError : public std::runtime_error {
 public:
  explicit ChannelError(const std::string& what) : std::runtime_error(what) {}
};

// A bidirectional, message-oriented channel over a byte stream.
class Channel {
 public:
  virtual ~Channel() = default;

  // Send one frame. Blocks until the whole frame is written.
  virtual void Send(Span<const std::uint8_t> payload) = 0;

  // Receive exactly one frame into `out`, resizing it. Blocks until the whole
  // frame has arrived. Throws ChannelError if the peer closes first.
  virtual void Recv(std::vector<std::uint8_t>& out) = 0;

  // No-op with blocking sockets and no user-space buffer; see the header
  // comment. Present because ARCHITECTURE section 8 names it and because a
  // buffered transport would need it.
  virtual void Flush() {}

  virtual void Close() = 0;

  // Bytes actually pushed through this channel, framing included. This is what
  // the benchmarks report, so it must count the 4-byte prefixes rather than
  // just the payloads.
  virtual std::uint64_t BytesSent() const = 0;
  virtual std::uint64_t BytesReceived() const = 0;
};

// --------------------------------------------------------------------------
//  Framing helpers, exposed so tests can exercise them without a socket.
// --------------------------------------------------------------------------

// Little-endian, explicit shifts. Not a memcpy of a uint32_t.
inline void EncodeLength(std::uint32_t n, std::uint8_t out[4]) {
  out[0] = static_cast<std::uint8_t>(n & 0xFF);
  out[1] = static_cast<std::uint8_t>((n >> 8) & 0xFF);
  out[2] = static_cast<std::uint8_t>((n >> 16) & 0xFF);
  out[3] = static_cast<std::uint8_t>((n >> 24) & 0xFF);
}

inline std::uint32_t DecodeLength(const std::uint8_t in[4]) {
  return static_cast<std::uint32_t>(in[0]) |
         (static_cast<std::uint32_t>(in[1]) << 8) |
         (static_cast<std::uint32_t>(in[2]) << 16) |
         (static_cast<std::uint32_t>(in[3]) << 24);
}

// Validate a declared length BEFORE it is used to size an allocation.
inline void CheckFrameLength(std::uint32_t n) {
  if (n > kMaxFrameBytes) {
    throw ChannelError("frame length " + std::to_string(n) +
                       " exceeds kMaxFrameBytes " +
                       std::to_string(kMaxFrameBytes) +
                       "; refusing to allocate on a peer's say-so");
  }
}

// --------------------------------------------------------------------------
//  TCP transport (src/net/tcp.cpp).
// --------------------------------------------------------------------------

// Connect to a listening peer. host is dotted-quad, e.g. "127.0.0.1".
std::unique_ptr<Channel> TcpConnect(const std::string& host, std::uint16_t port);

// A listening socket. One accept at a time; S1 servers are single-threaded and
// serve one client to completion, which is why no threading appears anywhere
// in this project.
class TcpListener {
 public:
  // recv_buffer_bytes > 0 shrinks SO_RCVBUF on the listening socket, which
  // accepted sockets inherit. Production passes 0; tests pass something tiny
  // to FORCE a frame to arrive in pieces, so the reassembly loop is exercised
  // deterministically rather than only when the network happens to oblige.
  explicit TcpListener(std::uint16_t port, int recv_buffer_bytes = 0);
  ~TcpListener();

  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;

  std::unique_ptr<Channel> Accept();
  std::uint16_t Port() const { return port_; }

 private:
  std::uint16_t port_;
  std::uintptr_t fd_;      // SOCKET, kept opaque so <winsock2.h> stays in the .cpp
};

// --------------------------------------------------------------------------
//  Transcript recording (src/net/transcript.cpp).
//
//  The 2026-09-06 Decisions Log row replaced the exit criterion's `tcpdump`
//  clause with a channel-boundary transcript, because there is no tcpdump on
//  Windows and because a transcript taken where the bytes are actually handed
//  to the socket is better provenance than a capture taken elsewhere.
// --------------------------------------------------------------------------

// Wrap a channel so every frame is also appended to `path` as JSONL:
// {dir, party, len, b64, timestamp}. The wrapper owns the inner channel.
std::unique_ptr<Channel> RecordTranscript(std::unique_ptr<Channel> inner,
                                          const std::string& path,
                                          const std::string& tag);

// Exposed for tests: the transcript writer's base64, so a round-trip through
// the recorded file can be checked rather than assumed.
std::string Base64Encode(Span<const std::uint8_t> bytes);
std::vector<std::uint8_t> Base64Decode(const std::string& text);

}  // namespace oblivrec
#endif  // OBLIVREC_CHANNEL_HPP
