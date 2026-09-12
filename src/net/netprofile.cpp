// ==========================================================================
//  netprofile.cpp -- see netprofile.hpp, especially on what this does not
//  model.
// ==========================================================================
#include "oblivrec/netprofile.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace oblivrec {
namespace {

class DelayedChannel final : public Channel {
 public:
  DelayedChannel(std::unique_ptr<Channel> inner, NetProfile p)
      : inner_(std::move(inner)), p_(p) {}

  void Send(Span<const std::uint8_t> payload) override {
    // Half the round trip on the way out, half on the way back in Recv, so a
    // request/response pair costs one RTT rather than two.
    Pay(payload.size(), p_.rtt_ms / 2.0);
    inner_->Send(payload);
  }

  void Recv(std::vector<std::uint8_t>& out) override {
    inner_->Recv(out);
    Pay(out.size(), p_.rtt_ms / 2.0);
  }

  void Flush() override { inner_->Flush(); }
  void Close() override { inner_->Close(); }
  std::uint64_t BytesSent() const override { return inner_->BytesSent(); }
  std::uint64_t BytesReceived() const override { return inner_->BytesReceived(); }

 private:
  void Pay(std::size_t bytes, double latency_ms) {
    double ms = latency_ms;
    if (p_.mbps > 0.0) {
      ms += (static_cast<double>(bytes) * 8.0 / (p_.mbps * 1e6)) * 1000.0;
    }
    if (ms <= 0.0) return;
    // sleep_for is coarse -- typically ~1 ms granularity on Windows -- so this
    // is honest for the 30 ms and 100 ms profiles and NOT for the 1 ms LAN
    // one. Said here rather than discovered from a suspiciously round number.
    std::this_thread::sleep_for(
        std::chrono::duration<double, std::milli>(ms));
  }

  std::unique_ptr<Channel> inner_;
  NetProfile p_;
};

}  // namespace

NetProfile ProfileByName(const std::string& name) {
  if (name == "local") return kProfileLocal;
  if (name == "lan") return kProfileLan;
  if (name == "wan_a") return kProfileWanA;
  if (name == "wan_b") return kProfileWanB;
  throw std::invalid_argument(
      "unknown network profile \"" + name +
      "\"; expected local, lan, wan_a or wan_b. Refusing rather than "
      "defaulting to local, which would silently mislabel a WAN result.");
}

std::unique_ptr<Channel> DelayChannel(std::unique_ptr<Channel> inner,
                                      const NetProfile& p) {
  return std::unique_ptr<Channel>(new DelayedChannel(std::move(inner), p));
}

}  // namespace oblivrec
