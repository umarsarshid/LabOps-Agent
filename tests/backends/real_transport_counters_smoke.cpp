#include "backends/real_sdk/transport_counters.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void Require(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

} // namespace

int main() {
  using labops::backends::BackendConfig;
  using labops::backends::real_sdk::CollectTransportCounters;

  // Common SDK aliases should map into one normalized counter snapshot.
  const BackendConfig available_dump = {
      {"GevResendPacketCount", "12"},
      {"GevPacketErrorCount", "3"},
      {"GevDroppedPacketCount", "7"},
  };
  const auto available = CollectTransportCounters(available_dump);
  Require(available.resends.available, "expected resend counter to be available");
  Require(available.resends.value == 12U, "unexpected resend counter value");
  Require(available.resends.source_key == "GevResendPacketCount", "unexpected resend source key");
  Require(available.packet_errors.available, "expected packet-error counter to be available");
  Require(available.packet_errors.value == 3U, "unexpected packet-error value");
  Require(available.dropped_packets.available, "expected dropped-packet counter to be available");
  Require(available.dropped_packets.value == 7U, "unexpected dropped-packet value");

  // Invalid or missing fields must not fail collection; they remain not
  // available and allow run execution to continue.
  const BackendConfig unavailable_dump = {
      {"transport.resends", "invalid"},
      {"transport.packet_errors", "-"},
      {"transport.dropped_packets", ""},
  };
  const auto unavailable = CollectTransportCounters(unavailable_dump);
  Require(!unavailable.resends.available, "resends should be unavailable for invalid input");
  Require(!unavailable.packet_errors.available,
          "packet_errors should be unavailable for invalid input");
  Require(!unavailable.dropped_packets.available,
          "dropped_packets should be unavailable for invalid input");

  std::cout << "real_transport_counters_smoke: ok\n";
  return 0;
}
