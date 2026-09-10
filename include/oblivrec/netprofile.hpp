// ==========================================================================
//  netprofile.hpp -- WAN behaviour without netem (task 3.10).
//
//  REQUIREMENTS section 6 asks for network profiles via `tc netem`:
//  local, lan (1ms/1Gbps), wan_a (30ms/100Mbps), wan_b (100ms/10Mbps).
//  There is no `tc` on this platform and no WSL to borrow one from --
//  `wsl --status` reports the subsystem is not installed -- so netem is not
//  available and neither is Docker.
//
//  Two substitutes, for the two halves of the system, because they are shaped
//  differently:
//
//    DelayChannel   For S1's real sockets. A Channel decorator, sitting
//                   alongside RecordTranscript, that injects per-frame latency
//                   and a bandwidth cap. Real bytes still cross a real socket;
//                   only the timing is imposed.
//
//    PredictedMs    For S2. Training does not transmit at all -- the substrate
//                   simulates three parties in one process and COUNTS what it
//                   would have sent. So the honest thing is not to fake a
//                   transmission but to apply the cost model to the counters:
//
//                       t = rounds * rtt + bytes / bandwidth
//
//  ------------------------------------------------------------------------
//  STATE THE LIMITATION, BECAUSE IT IS A REAL ONE.
//
//  This shapes traffic at the CHANNEL BOUNDARY, not at the link. It models no
//  queueing, no loss, no congestion control and no TCP slow-start, so it will
//  not reproduce the tail behaviour of a real wide-area link.
//
//  What it DOES model exactly is round count and bytes -- those are counted,
//  not estimated -- and for an MPC protocol on a wide-area link those are what
//  dominate. That is the whole reason round complexity is the thing this
//  design optimises. Any number produced through here must be labelled as
//  channel-level emulation, never as a netem measurement.
// ==========================================================================
#ifndef OBLIVREC_NETPROFILE_HPP
#define OBLIVREC_NETPROFILE_HPP

#include <cstdint>
#include <memory>
#include <string>

#include "oblivrec/channel.hpp"

namespace oblivrec {

struct NetProfile {
  const char* name;
  double rtt_ms;      // round-trip time
  double mbps;        // bandwidth, megabits per second
};

// The four profiles REQUIREMENTS section 6 names.
constexpr NetProfile kProfileLocal{"local", 0.0, 0.0};        // 0 mbps = no cap
constexpr NetProfile kProfileLan{"lan", 1.0, 1000.0};
constexpr NetProfile kProfileWanA{"wan_a", 30.0, 100.0};
constexpr NetProfile kProfileWanB{"wan_b", 100.0, 10.0};

// Look a profile up by name; throws on an unknown one rather than defaulting,
// because silently falling back to `local` would make a WAN result a lie.
NetProfile ProfileByName(const std::string& name);

// The cost model. `rounds` and `bytes` come from Mpc3's counters, which are
// measured rather than estimated, so the only modelled quantity here is the
// link itself.
inline double PredictedMs(std::uint64_t rounds, std::uint64_t bytes,
                          const NetProfile& p) {
  const double latency = static_cast<double>(rounds) * p.rtt_ms;
  const double transfer =
      (p.mbps > 0.0)
          ? (static_cast<double>(bytes) * 8.0 / (p.mbps * 1e6)) * 1000.0
          : 0.0;
  return latency + transfer;
}

// Which of the two terms dominates. Reported alongside every prediction,
// because "round-bound" versus "bandwidth-bound" is the actionable half of the
// answer and a single millisecond figure hides it.
inline const char* Bottleneck(std::uint64_t rounds, std::uint64_t bytes,
                              const NetProfile& p) {
  const double latency = static_cast<double>(rounds) * p.rtt_ms;
  const double transfer =
      (p.mbps > 0.0)
          ? (static_cast<double>(bytes) * 8.0 / (p.mbps * 1e6)) * 1000.0
          : 0.0;
  if (latency == 0.0 && transfer == 0.0) return "none";
  return (latency >= transfer) ? "rounds" : "bandwidth";
}

// Wrap a real channel so every frame pays the profile's latency and bandwidth.
// The bytes are genuine and still cross a socket; only the timing is imposed.
std::unique_ptr<Channel> DelayChannel(std::unique_ptr<Channel> inner,
                                      const NetProfile& p);

}  // namespace oblivrec
#endif  // OBLIVREC_NETPROFILE_HPP
