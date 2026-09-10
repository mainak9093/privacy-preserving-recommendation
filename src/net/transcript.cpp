// ==========================================================================
//  transcript.cpp -- record every frame at the CHANNEL BOUNDARY.
//
//  This is the evidence half of the Phase 2 exit criterion. The original
//  wording asked for `tcpdump` on the server links; the 2026-09-06 Decisions
//  Log row replaced that, for two reasons. There is no tcpdump on Windows,
//  and a transcript taken where the bytes are handed to the socket is better
//  provenance than a capture taken somewhere else and correlated afterwards.
//
//  What is recorded is exactly what crosses the boundary: direction, the tag
//  identifying which link, the frame length, and the payload. Nothing is
//  summarised or filtered here -- summarising is the analysis script's job,
//  and a transcript that had already made judgements would not be evidence.
//
//  The payload is base64'd because the file is JSONL and the bytes are
//  arbitrary. Base64 is decoded back in tests, so the encoder is checked
//  rather than trusted.
// ==========================================================================
#include "oblivrec/channel.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>

namespace oblivrec {
namespace {

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int B64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::string NowStamp() {
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

// A channel that forwards everything and writes down what it forwarded.
class TranscriptChannel final : public Channel {
 public:
  TranscriptChannel(std::unique_ptr<Channel> inner, const std::string& path,
                    std::string tag)
      : inner_(std::move(inner)), tag_(std::move(tag)) {
    out_.open(path, std::ios::app);
    if (!out_) {
      throw ChannelError("cannot open transcript file " + path);
    }
  }

  void Send(Span<const std::uint8_t> payload) override {
    inner_->Send(payload);
    Record("send", payload);
  }

  void Recv(std::vector<std::uint8_t>& out) override {
    inner_->Recv(out);
    Record("recv", Span<const std::uint8_t>(out.data(), out.size()));
  }

  void Flush() override {
    inner_->Flush();
    out_.flush();
  }

  void Close() override {
    inner_->Close();
    if (out_.is_open()) out_.close();
  }

  std::uint64_t BytesSent() const override { return inner_->BytesSent(); }
  std::uint64_t BytesReceived() const override { return inner_->BytesReceived(); }

 private:
  void Record(const char* dir, Span<const std::uint8_t> payload) {
    out_ << "{\"tag\": \"" << tag_ << "\", \"dir\": \"" << dir
         << "\", \"len\": " << payload.size()
         << ", \"b64\": \"" << Base64Encode(payload)
         << "\", \"timestamp\": \"" << NowStamp() << "\"}\n";
  }

  std::unique_ptr<Channel> inner_;
  std::string tag_;
  std::ofstream out_;
};

}  // namespace

std::string Base64Encode(Span<const std::uint8_t> bytes) {
  std::string out;
  const std::size_t n = bytes.size();
  out.reserve(((n + 2) / 3) * 4);
  std::size_t i = 0;
  for (; i + 2 < n; i += 3) {
    const std::uint32_t v = (std::uint32_t(bytes[i]) << 16) |
                            (std::uint32_t(bytes[i + 1]) << 8) |
                            std::uint32_t(bytes[i + 2]);
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    out += kB64[(v >> 6) & 63];
    out += kB64[v & 63];
  }
  if (i < n) {
    std::uint32_t v = std::uint32_t(bytes[i]) << 16;
    const bool two = (i + 1 < n);
    if (two) v |= std::uint32_t(bytes[i + 1]) << 8;
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    out += two ? kB64[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

std::vector<std::uint8_t> Base64Decode(const std::string& text) {
  std::vector<std::uint8_t> out;
  out.reserve(text.size() / 4 * 3);
  std::uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    const int v = B64Value(c);
    if (v < 0) throw ChannelError("invalid base64 character in transcript");
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

std::unique_ptr<Channel> RecordTranscript(std::unique_ptr<Channel> inner,
                                          const std::string& path,
                                          const std::string& tag) {
  return std::unique_ptr<Channel>(
      new TranscriptChannel(std::move(inner), path, tag));
}

}  // namespace oblivrec
