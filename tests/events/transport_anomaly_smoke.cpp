#include "events/transport_anomaly.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

bool ContainsSummarySubstring(const std::vector<labops::events::TransportAnomalyFinding>& findings,
                              std::string_view needle) {
  for (const auto& finding : findings) {
    if (finding.summary.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  labops::core::schema::RunInfo run_info;
  run_info.run_id = "run-transport-anomaly-smoke";
  run_info.config.scenario_id = "real_transport_anomaly_smoke";
  run_info.config.backend = "real_stub";
  run_info.real_device = labops::core::schema::RealDeviceMetadata{
      .model = "SprintCam",
      .serial = "SN-42",
      .transport = "gige",
      .user_id = std::nullopt,
      .firmware_version = std::nullopt,
      .sdk_version = std::nullopt,
  };

  run_info.real_device->transport_counters.resends.available = true;
  run_info.real_device->transport_counters.resends.value = 120U;
  run_info.real_device->transport_counters.packet_errors.available = true;
  run_info.real_device->transport_counters.packet_errors.value = 2U;
  run_info.real_device->transport_counters.dropped_packets.available = true;
  run_info.real_device->transport_counters.dropped_packets.value = 0U;

  const std::vector<labops::events::TransportAnomalyFinding> findings =
      labops::events::DetectTransportAnomalies(run_info);
  if (findings.size() != 2U) {
    Fail("expected two transport anomaly findings");
  }
  if (!ContainsSummarySubstring(findings, "resend spike")) {
    Fail("expected resend spike anomaly summary");
  }
  if (!ContainsSummarySubstring(findings, "packet errors")) {
    Fail("expected packet-errors anomaly summary");
  }

  // Counters that are unavailable should not produce heuristic findings.
  run_info.real_device->transport_counters.resends.available = false;
  run_info.real_device->transport_counters.resends.value.reset();
  run_info.real_device->transport_counters.packet_errors.available = false;
  run_info.real_device->transport_counters.packet_errors.value.reset();
  run_info.real_device->transport_counters.dropped_packets.available = false;
  run_info.real_device->transport_counters.dropped_packets.value.reset();

  const std::vector<labops::events::TransportAnomalyFinding> unavailable_findings =
      labops::events::DetectTransportAnomalies(run_info);
  if (!unavailable_findings.empty()) {
    Fail("expected no findings when transport counters are unavailable");
  }

  std::cout << "transport_anomaly_smoke: ok\n";
  return 0;
}
