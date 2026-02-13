#include "core/schema/run_contract.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

namespace {

// Keep expected snapshots readable while still validating full output shape.
void RequireContains(const std::string& text, const std::string& needle) {
  REQUIRE(text.find(needle) != std::string::npos);
}

} // namespace

TEST_CASE("RunConfig JSON serialization includes required fields", "[core][schema][json]") {
  labops::core::schema::RunConfig config;
  config.scenario_id = "sim_baseline";
  config.backend = "sim";
  config.seed = 42;
  config.duration = std::chrono::minutes(10);

  const std::string json = labops::core::schema::ToJson(config);
  REQUIRE(json ==
          R"({"scenario_id":"sim_baseline","backend":"sim","seed":42,"duration_ms":600000})");
}

TEST_CASE("RunInfo JSON serialization includes config and timestamps", "[core][schema][json]") {
  labops::core::schema::RunInfo info;
  info.run_id = "run-1000";
  info.config.scenario_id = "dropped_frames";
  info.config.backend = "sim";
  info.config.seed = 7;
  info.config.duration = std::chrono::milliseconds(1'500);
  info.timestamps.created_at =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'000));
  info.timestamps.started_at =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'250));
  info.timestamps.finished_at =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(2'500));

  const std::string json = labops::core::schema::ToJson(info);
  RequireContains(json, "\"run_id\":\"run-1000\"");
  RequireContains(json, "\"scenario_id\":\"dropped_frames\"");
  RequireContains(json, "\"backend\":\"sim\"");
  RequireContains(json, "\"seed\":7");
  RequireContains(json, "\"duration_ms\":1500");
  RequireContains(json, "\"created_at_utc\":\"1970-01-01T00:00:01.000Z\"");
  RequireContains(json, "\"started_at_utc\":\"1970-01-01T00:00:01.250Z\"");
  RequireContains(json, "\"finished_at_utc\":\"1970-01-01T00:00:02.500Z\"");
}
