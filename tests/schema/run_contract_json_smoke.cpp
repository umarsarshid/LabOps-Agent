#include "core/schema/run_contract.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected JSON to contain: " << needle << '\n';
    std::cerr << "actual JSON: " << text << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  using labops::core::schema::RunConfig;
  using labops::core::schema::RunInfo;
  using labops::core::schema::ToJson;

  RunConfig config;
  config.scenario_id = "sim_baseline";
  config.backend = "sim";
  config.seed = 42;
  config.duration = std::chrono::minutes(10);

  const std::string config_json = ToJson(config);
  AssertContains(config_json, "\"scenario_id\":\"sim_baseline\"");
  AssertContains(config_json, "\"backend\":\"sim\"");
  AssertContains(config_json, "\"seed\":42");
  AssertContains(config_json, "\"duration_ms\":600000");

  RunInfo info;
  info.run_id = "run-000001";
  info.config = config;
  info.webcam_device = labops::core::schema::WebcamDeviceMetadata{
      .device_id = "webcam-0",
      .friendly_name = "DeskCam",
      .bus_info = std::optional<std::string>("usb:1-2"),
      .selector_text = std::optional<std::string>("id:webcam-0"),
      .selection_rule = std::optional<std::string>("id"),
      .discovered_index = std::optional<std::uint64_t>(0),
  };
  info.real_device = labops::core::schema::RealDeviceMetadata{
      .model = "SprintCam",
      .serial = "SN-2000",
      .transport = "usb",
      .user_id = std::optional<std::string>("LineA"),
      .firmware_version = std::optional<std::string>("1.2.3"),
      .sdk_version = std::optional<std::string>("21.1.8"),
  };
  info.real_device->transport_counters.resends.available = true;
  info.real_device->transport_counters.resends.value = 14U;
  info.real_device->transport_counters.packet_errors.available = false;
  info.real_device->transport_counters.packet_errors.value.reset();
  info.real_device->transport_counters.dropped_packets.available = true;
  info.real_device->transport_counters.dropped_packets.value = 2U;

  // Use deterministic timestamps so the smoke test is stable.
  const auto created_at = std::chrono::system_clock::time_point(std::chrono::milliseconds(1000));
  const auto started_at = std::chrono::system_clock::time_point(std::chrono::milliseconds(2500));
  const auto finished_at = std::chrono::system_clock::time_point(std::chrono::milliseconds(3500));

  info.timestamps.created_at = created_at;
  info.timestamps.started_at = started_at;
  info.timestamps.finished_at = finished_at;

  const std::string run_info_json = ToJson(info);
  AssertContains(run_info_json, "\"run_id\":\"run-000001\"");
  AssertContains(run_info_json, "\"config\":");
  AssertContains(run_info_json, "\"real_device\":");
  AssertContains(run_info_json, "\"webcam_device\":");
  AssertContains(run_info_json, "\"device_id\":\"webcam-0\"");
  AssertContains(run_info_json, "\"friendly_name\":\"DeskCam\"");
  AssertContains(run_info_json, "\"bus_info\":\"usb:1-2\"");
  AssertContains(run_info_json, "\"selector\":\"id:webcam-0\"");
  AssertContains(run_info_json, "\"selection_rule\":\"id\"");
  AssertContains(run_info_json, "\"discovered_index\":0");
  AssertContains(run_info_json, "\"model\":\"SprintCam\"");
  AssertContains(run_info_json, "\"serial\":\"SN-2000\"");
  AssertContains(run_info_json, "\"transport\":\"usb\"");
  AssertContains(run_info_json, "\"firmware_version\":\"1.2.3\"");
  AssertContains(run_info_json, "\"sdk_version\":\"21.1.8\"");
  AssertContains(run_info_json, "\"transport_counters\":");
  AssertContains(run_info_json, "\"resends\":{\"status\":\"available\",\"value\":14}");
  AssertContains(run_info_json, "\"packet_errors\":{\"status\":\"not_available\"}");
  AssertContains(run_info_json, "\"dropped_packets\":{\"status\":\"available\",\"value\":2}");
  AssertContains(run_info_json, "\"timestamps\":");
  AssertContains(run_info_json, "\"created_at_utc\":\"1970-01-01T00:00:01.000Z\"");
  AssertContains(run_info_json, "\"started_at_utc\":\"1970-01-01T00:00:02.500Z\"");
  AssertContains(run_info_json, "\"finished_at_utc\":\"1970-01-01T00:00:03.500Z\"");

  std::cout << "run_contract_json_smoke: ok\n";
  return 0;
}
