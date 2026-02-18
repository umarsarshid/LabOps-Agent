#include "core/schema/run_contract.hpp"
#include "core/json_utils.hpp"
#include "core/time_utils.hpp"

#include <sstream>

namespace labops::core::schema {

std::string ToJson(const RunConfig& run_config) {
  std::ostringstream out;
  out << "{"
      << "\"scenario_id\":\"" << core::EscapeJson(run_config.scenario_id) << "\","
      << "\"backend\":\"" << core::EscapeJson(run_config.backend) << "\","
      << "\"seed\":" << run_config.seed << ","
      << "\"duration_ms\":" << run_config.duration.count() << "}";
  return out.str();
}

std::string ToJson(const TransportCounterStatus& counter) {
  std::ostringstream out;
  out << "{";
  if (counter.available && counter.value.has_value()) {
    out << "\"status\":\"available\","
        << "\"value\":" << counter.value.value();
  } else {
    out << "\"status\":\"not_available\"";
  }
  out << "}";
  return out.str();
}

std::string ToJson(const TransportCounterSnapshot& counters) {
  std::ostringstream out;
  out << "{"
      << "\"resends\":" << ToJson(counters.resends) << ","
      << "\"packet_errors\":" << ToJson(counters.packet_errors) << ","
      << "\"dropped_packets\":" << ToJson(counters.dropped_packets) << "}";
  return out.str();
}

std::string ToJson(const RealDeviceMetadata& real_device) {
  std::ostringstream out;
  out << "{"
      << "\"model\":\"" << core::EscapeJson(real_device.model) << "\","
      << "\"serial\":\"" << core::EscapeJson(real_device.serial) << "\","
      << "\"transport\":\"" << core::EscapeJson(real_device.transport) << "\"";
  if (real_device.user_id.has_value()) {
    out << ",\"user_id\":\"" << core::EscapeJson(real_device.user_id.value()) << "\"";
  }
  if (real_device.firmware_version.has_value()) {
    out << ",\"firmware_version\":\"" << core::EscapeJson(real_device.firmware_version.value())
        << "\"";
  }
  if (real_device.sdk_version.has_value()) {
    out << ",\"sdk_version\":\"" << core::EscapeJson(real_device.sdk_version.value()) << "\"";
  }
  out << ",\"transport_counters\":" << ToJson(real_device.transport_counters);
  out << "}";
  return out.str();
}

std::string ToJson(const RunInfo& run_info) {
  std::ostringstream out;
  out << "{"
      << "\"run_id\":\"" << core::EscapeJson(run_info.run_id) << "\","
      << "\"config\":" << ToJson(run_info.config);
  if (run_info.real_device.has_value()) {
    out << ",\"real_device\":" << ToJson(run_info.real_device.value());
  }
  out << ","
      << "\"timestamps\":{"
      << "\"created_at_utc\":\"" << core::FormatUtcTimestamp(run_info.timestamps.created_at)
      << "\","
      << "\"started_at_utc\":\"" << core::FormatUtcTimestamp(run_info.timestamps.started_at)
      << "\","
      << "\"finished_at_utc\":\"" << core::FormatUtcTimestamp(run_info.timestamps.finished_at)
      << "\""
      << "}"
      << "}";
  return out.str();
}

} // namespace labops::core::schema
