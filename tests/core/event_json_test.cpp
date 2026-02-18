#include "events/event_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

TEST_CASE("EventType maps to stable string values", "[core][events][json]") {
  REQUIRE(labops::events::ToJson(labops::events::EventType::kRunStarted) == "run_started");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kConfigApplied) == "CONFIG_APPLIED");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kConfigUnsupported) ==
          "CONFIG_UNSUPPORTED");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kConfigAdjusted) == "CONFIG_ADJUSTED");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kTransportAnomaly) ==
          "TRANSPORT_ANOMALY");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kInfo) == "info");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kWarning) == "warning");
  REQUIRE(labops::events::ToJson(labops::events::EventType::kError) == "error");
}

TEST_CASE("Event JSON serialization includes timestamp type and payload", "[core][events][json]") {
  labops::events::Event event;
  event.ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(2'000));
  event.type = labops::events::EventType::kRunStarted;
  event.payload = {
      {"backend", "sim"},
      {"run_id", "run-2000"},
  };

  const std::string json = labops::events::ToJson(event);
  REQUIRE(
      json ==
      R"({"ts_utc":"1970-01-01T00:00:02.000Z","type":"run_started","payload":{"backend":"sim","run_id":"run-2000"}})");
}
