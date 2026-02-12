#include "core/schema/run_contract.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
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
  AssertContains(run_info_json, "\"timestamps\":");
  AssertContains(run_info_json, "\"created_at_utc\":\"1970-01-01T00:00:01.000Z\"");
  AssertContains(run_info_json, "\"started_at_utc\":\"1970-01-01T00:00:02.500Z\"");
  AssertContains(run_info_json, "\"finished_at_utc\":\"1970-01-01T00:00:03.500Z\"");

  std::cout << "run_contract_json_smoke: ok\n";
  return 0;
}
