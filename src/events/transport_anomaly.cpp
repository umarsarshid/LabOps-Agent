#include "events/transport_anomaly.hpp"

#include <vector>

namespace labops::events {

namespace {

constexpr std::uint64_t kResendSpikeThreshold = 50U;
constexpr std::uint64_t kPacketErrorThreshold = 1U;
constexpr std::uint64_t kDroppedPacketThreshold = 1U;

void MaybeAppendFinding(std::vector<TransportAnomalyFinding>& findings,
                        std::string heuristic_id, std::string counter_name,
                        const core::schema::TransportCounterStatus& counter_status,
                        const std::uint64_t threshold, std::string summary_prefix) {
  if (!counter_status.available || !counter_status.value.has_value()) {
    return;
  }
  if (counter_status.value.value() < threshold) {
    return;
  }

  const std::uint64_t observed_value = counter_status.value.value();
  findings.push_back(TransportAnomalyFinding{
      .heuristic_id = std::move(heuristic_id),
      .counter_name = std::move(counter_name),
      .observed_value = observed_value,
      .threshold = threshold,
      .summary = std::move(summary_prefix) + " counter " + std::to_string(observed_value) +
                 " exceeded threshold " + std::to_string(threshold) + ".",
  });
}

} // namespace

std::vector<TransportAnomalyFinding>
DetectTransportAnomalies(const core::schema::RunInfo& run_info) {
  std::vector<TransportAnomalyFinding> findings;
  if (!run_info.real_device.has_value()) {
    return findings;
  }

  const core::schema::TransportCounterSnapshot& counters = run_info.real_device->transport_counters;
  findings.reserve(3U);

  // Keep order deterministic so summary/event output is predictable run-to-run.
  MaybeAppendFinding(findings, "resend_spike_threshold", "resends", counters.resends,
                     kResendSpikeThreshold, "Transport anomaly: resend spike");
  MaybeAppendFinding(findings, "packet_error_threshold", "packet_errors", counters.packet_errors,
                     kPacketErrorThreshold, "Transport anomaly: packet errors");
  MaybeAppendFinding(findings, "dropped_packet_threshold", "dropped_packets",
                     counters.dropped_packets, kDroppedPacketThreshold,
                     "Transport anomaly: dropped packets");

  return findings;
}

} // namespace labops::events
