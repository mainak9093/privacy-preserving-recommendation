// ==========================================================================
//  tcp.cpp -- the Winsock transport behind channel.hpp.
//
//  Blocking sockets, one client at a time. That is not a simplification we
//  will regret: an S1 server serves one client to completion, so there is
//  nothing concurrent to express, and choosing separate PROCESSES over threads
//  (Decisions Log, 2026-09-10) means no threading appears in this project at
//  all.
//
//  WSAStartup is refcounted per process and is done lazily on first use, so a
//  caller cannot forget it and a test that never opens a socket never pays it.
// ==========================================================================
#include "oblivrec/channel.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

namespace oblivrec {
namespace {

// WSAStartup once per process. Function-local static initialisation is
// thread-safe in C++11 and later, and costs nothing when unused.
void EnsureWinsock() {
  static const bool started = [] {
    WSADATA wsa;
    const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
      throw ChannelError("WSAStartup failed with " + std::to_string(rc));
    }
    return true;
  }();
  (void)started;
}

std::string SockErr(const char* what) {
  return std::string(what) + " failed, WSAGetLastError=" +
         std::to_string(WSAGetLastError());
}

// ------------------------------------------------------------------------
//  A connected socket, framed.
// ------------------------------------------------------------------------
class TcpChannel final : public Channel {
 public:
  explicit TcpChannel(SOCKET s) : sock_(s) {}
  ~TcpChannel() override { Close(); }

  void Send(Span<const std::uint8_t> payload) override {
    if (payload.size() > kMaxFrameBytes) {
      throw ChannelError("refusing to send a frame of " +
                         std::to_string(payload.size()) + " bytes");
    }
    std::uint8_t hdr[4];
    EncodeLength(static_cast<std::uint32_t>(payload.size()), hdr);
    SendAll(hdr, 4);
    if (payload.size() > 0) SendAll(&payload[0], payload.size());
  }

  void Recv(std::vector<std::uint8_t>& out) override {
    std::uint8_t hdr[4];
    RecvAll(hdr, 4);
    const std::uint32_t n = DecodeLength(hdr);
    // BEFORE the resize, not after. See channel.hpp.
    CheckFrameLength(n);
    out.resize(n);
    if (n > 0) RecvAll(out.data(), n);
  }

  void Close() override {
    if (sock_ != INVALID_SOCKET) {
      ::shutdown(sock_, SD_BOTH);
      ::closesocket(sock_);
      sock_ = INVALID_SOCKET;
    }
  }

  std::uint64_t BytesSent() const override { return sent_; }
  std::uint64_t BytesReceived() const override { return recvd_; }

 private:
  // send() may accept fewer bytes than offered. Looping is not optional.
  void SendAll(const std::uint8_t* p, std::size_t n) {
    std::size_t done = 0;
    while (done < n) {
      const int chunk = static_cast<int>(
          (n - done) > 65536 ? 65536 : (n - done));
      const int k = ::send(sock_, reinterpret_cast<const char*>(p + done),
                           chunk, 0);
      if (k == SOCKET_ERROR) throw ChannelError(SockErr("send"));
      if (k == 0) throw ChannelError("send returned 0; peer closed");
      done += static_cast<std::size_t>(k);
      sent_ += static_cast<std::uint64_t>(k);
    }
  }

  // THE LOOP THAT MATTERS. TCP is a stream: a 227-byte key can arrive as
  // 200 + 27, and on a real link it will. tests/test_channel.cpp forces a
  // split rather than waiting to be unlucky in production.
  void RecvAll(std::uint8_t* p, std::size_t n) {
    std::size_t done = 0;
    while (done < n) {
      const int chunk = static_cast<int>(
          (n - done) > 65536 ? 65536 : (n - done));
      const int k = ::recv(sock_, reinterpret_cast<char*>(p + done), chunk, 0);
      if (k == SOCKET_ERROR) throw ChannelError(SockErr("recv"));
      if (k == 0) {
        throw ChannelError("peer closed after " + std::to_string(done) +
                           " of " + std::to_string(n) + " expected bytes");
      }
      done += static_cast<std::size_t>(k);
      recvd_ += static_cast<std::uint64_t>(k);
    }
  }

  SOCKET sock_ = INVALID_SOCKET;
  std::uint64_t sent_ = 0, recvd_ = 0;
};

}  // namespace

std::unique_ptr<Channel> TcpConnect(const std::string& host,
                                    std::uint16_t port) {
  EnsureWinsock();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) throw ChannelError(SockErr("socket"));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    ::closesocket(s);
    throw ChannelError("not a dotted-quad address: " + host);
  }

  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    const int e = WSAGetLastError();
    ::closesocket(s);
    throw ChannelError("connect to " + host + ":" + std::to_string(port) +
                       " failed (WSAGetLastError=" + std::to_string(e) +
                       "). Are the servers running? See scripts/run_servers.sh");
  }

  // Nagle would coalesce our small frames and add latency for no benefit; the
  // framing layer already batches at the message level.
  int one = 1;
  ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one),
               sizeof(one));

  return std::unique_ptr<Channel>(new TcpChannel(s));
}

TcpListener::TcpListener(std::uint16_t port, int recv_buffer_bytes)
    : port_(port), fd_(0) {
  EnsureWinsock();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) throw ChannelError(SockErr("socket"));

  // Set before listen() so accepted sockets inherit it. Tests use this to
  // force multi-part delivery; production leaves it at the OS default.
  if (recv_buffer_bytes > 0) {
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<char*>(&recv_buffer_bytes),
                 sizeof(recv_buffer_bytes));
  }

  // NOT SO_REUSEADDR. On Windows that permits another process to HIJACK a
  // port already in use, which turns "the servers are already running" from a
  // loud failure into a silent, confusing one. SO_EXCLUSIVEADDRUSE is the
  // Windows-correct choice and makes a double launch fail immediately.
  int one = 1;
  ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<char*>(&one), sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // loopback only, never 0.0.0.0

  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    const int e = WSAGetLastError();
    ::closesocket(s);
    throw ChannelError("bind to port " + std::to_string(port) +
                       " failed (WSAGetLastError=" + std::to_string(e) +
                       "). Port in use -- servers already running?");
  }
  if (::listen(s, 4) == SOCKET_ERROR) {
    const int e = WSAGetLastError();
    ::closesocket(s);
    throw ChannelError("listen failed, WSAGetLastError=" + std::to_string(e));
  }

  // Port 0 means "pick one"; ask which, so tests can bind without a fixed port
  // and therefore without colliding with a running server.
  if (port == 0) {
    sockaddr_in actual{};
    int len = sizeof(actual);
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&actual), &len) == 0) {
      port_ = ntohs(actual.sin_port);
    }
  }
  fd_ = static_cast<std::uintptr_t>(s);
}

TcpListener::~TcpListener() {
  if (fd_ != 0) ::closesocket(static_cast<SOCKET>(fd_));
}

std::unique_ptr<Channel> TcpListener::Accept() {
  SOCKET c = ::accept(static_cast<SOCKET>(fd_), nullptr, nullptr);
  if (c == INVALID_SOCKET) throw ChannelError(SockErr("accept"));
  int one = 1;
  ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one),
               sizeof(one));
  return std::unique_ptr<Channel>(new TcpChannel(c));
}

}  // namespace oblivrec
