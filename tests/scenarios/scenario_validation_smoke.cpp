#include "../common/assertions.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"
#include "scenarios/validator.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::Fail;
using labops::tests::common::WriteFixtureFile;
using labops::tests::common::WriteScenarioFixture;

bool ContainsIssue(const labops::scenarios::ValidationReport& report, std::string_view path,
                   std::string_view message_substring) {
  for (const auto& issue : report.issues) {
    if (issue.path == path && issue.message.find(message_substring) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  {
    const std::string valid_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "baseline_smoke",
  "netem_profile": "jitter_light",
  "duration": {
    "duration_ms": 10000
  },
  "camera": {
    "fps": 30,
    "pixel_format": "mono8",
    "trigger_mode": "free_run"
  },
  "thresholds": {
    "min_avg_fps": 28.0
  }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(valid_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for valid json: " + error);
    }
    if (!report.valid || !report.issues.empty()) {
      Fail("expected valid scenario to produce zero validation issues");
    }
  }

  {
    const std::string valid_real_selector_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "real_selector_smoke",
  "backend": "real_stub",
  "device_selector": "serial:SN-1001,index:0",
  "duration": {
    "duration_ms": 2000
  },
  "camera": {
    "fps": 30,
    "trigger_mode": "free_run"
  },
  "thresholds": {
    "min_avg_fps": 1.0
  }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(valid_real_selector_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for valid real selector json: " + error);
    }
    if (!report.valid || !report.issues.empty()) {
      Fail("expected valid real selector scenario to produce zero validation issues");
    }
  }

  {
    const std::string valid_webcam_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "webcam_selector_smoke",
  "backend": "webcam",
  "duration": { "duration_ms": 2000 },
  "camera": { "fps": 30 },
  "webcam": {
    "device_selector": {
      "index": 0,
      "name_contains": "usb"
    },
    "requested_width": 1280,
    "requested_height": 720,
    "requested_fps": 29.97,
    "requested_pixel_format": "MJPG"
  },
  "thresholds": { "min_avg_fps": 1.0 }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(valid_webcam_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for valid webcam json: " + error);
    }
    if (!report.valid || !report.issues.empty()) {
      Fail("expected valid webcam scenario to produce zero validation issues");
    }
  }

  {
    const fs::path temp_root = CreateUniqueTempDir("labops-netem-profile-smoke");
    const fs::path tools_dir = temp_root / "tools" / "netem_profiles";
    const fs::path scenarios_dir = temp_root / "scenarios";
    const fs::path valid_scenario_path = scenarios_dir / "valid_with_profile.json";
    const fs::path invalid_scenario_path = scenarios_dir / "missing_profile.json";
    const fs::path profile_path = tools_dir / "jitter_light.json";

    std::error_code ec;
    fs::create_directories(tools_dir, ec);
    fs::create_directories(scenarios_dir, ec);
    if (ec) {
      Fail("failed to create temp paths for netem profile validation smoke");
    }

    WriteFixtureFile(profile_path,
                     "{\n"
                     "  \"profile_id\": \"jitter_light\",\n"
                     "  \"description\": \"smoke profile\",\n"
                     "  \"netem\": { \"delay_ms\": 5, \"jitter_ms\": 2, \"loss_percent\": 0, "
                     "\"reorder_percent\": 0 }\n"
                     "}\n");

    WriteScenarioFixture(valid_scenario_path, "{\n"
                                              "  \"schema_version\": \"1.0\",\n"
                                              "  \"scenario_id\": \"valid_with_profile\",\n"
                                              "  \"netem_profile\": \"jitter_light\",\n"
                                              "  \"duration\": {\"duration_ms\": 1000},\n"
                                              "  \"camera\": {\"fps\": 30},\n"
                                              "  \"thresholds\": {\"min_avg_fps\": 10}\n"
                                              "}\n");

    WriteScenarioFixture(invalid_scenario_path, "{\n"
                                                "  \"schema_version\": \"1.0\",\n"
                                                "  \"scenario_id\": \"missing_profile\",\n"
                                                "  \"netem_profile\": \"does_not_exist\",\n"
                                                "  \"duration\": {\"duration_ms\": 1000},\n"
                                                "  \"camera\": {\"fps\": 30},\n"
                                                "  \"thresholds\": {\"min_avg_fps\": 10}\n"
                                                "}\n");

    {
      labops::scenarios::ValidationReport report;
      std::string error;
      if (!labops::scenarios::ValidateScenarioFile(valid_scenario_path.string(), report, error)) {
        Fail("ValidateScenarioFile failed unexpectedly for valid netem profile scenario: " + error);
      }
      if (!report.valid || !report.issues.empty()) {
        Fail("expected scenario with existing netem profile to pass validation");
      }
    }

    {
      labops::scenarios::ValidationReport report;
      std::string error;
      if (!labops::scenarios::ValidateScenarioFile(invalid_scenario_path.string(), report, error)) {
        Fail("ValidateScenarioFile failed unexpectedly for missing netem profile scenario: " +
             error);
      }
      if (report.valid) {
        Fail("expected scenario with missing netem profile to fail validation");
      }
      if (!ContainsIssue(report, "netem_profile", "not found under tools/netem_profiles")) {
        Fail("missing actionable issue for netem_profile missing file");
      }
    }

    fs::remove_all(temp_root, ec);
  }

  {
    const std::string invalid_webcam_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "webcam_invalid_fields",
  "backend": "webcam",
  "duration": { "duration_ms": 1000 },
  "camera": { "fps": 20 },
  "webcam": {
    "device_selector": {},
    "requested_width": 0,
    "requested_height": -1,
    "requested_fps": 0,
    "requested_pixel_format": ""
  },
  "thresholds": { "min_avg_fps": 1.0 }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(invalid_webcam_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for invalid webcam json: " + error);
    }
    if (report.valid) {
      Fail("expected invalid webcam scenario to fail validation");
    }
    if (!ContainsIssue(report, "webcam.device_selector", "at least one selector key")) {
      Fail("missing actionable issue for webcam.device_selector");
    }
    if (!ContainsIssue(report, "webcam.requested_width", "positive integer")) {
      Fail("missing actionable issue for webcam.requested_width");
    }
    if (!ContainsIssue(report, "webcam.requested_height", "positive integer")) {
      Fail("missing actionable issue for webcam.requested_height");
    }
    if (!ContainsIssue(report, "webcam.requested_fps", "positive number")) {
      Fail("missing actionable issue for webcam.requested_fps");
    }
    if (!ContainsIssue(report, "webcam.requested_pixel_format", "non-empty string")) {
      Fail("missing actionable issue for webcam.requested_pixel_format");
    }
  }

  {
    const std::string invalid_schema_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "Bad Id",
  "backend": "bad_backend",
  "device_selector": "serial:",
  "duration": { "duration_ms": 0 },
  "camera": {
    "fps": 0,
    "trigger_mode": "edge",
    "trigger_source": "line7",
    "trigger_activation": "upward",
    "roi": { "x": 10, "width": 640 }
  },
  "sim_faults": { "drop_percent": 120 },
  "thresholds": {},
  "oaat": {
    "enabled": true,
    "variables": [
      { "path": "", "values": [] }
    ]
  }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(invalid_schema_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for invalid schema json: " + error);
    }
    if (report.valid) {
      Fail("expected invalid scenario to fail validation");
    }

    if (!ContainsIssue(report, "scenario_id", "lowercase slug")) {
      Fail("missing actionable issue for scenario_id slug format");
    }
    if (!ContainsIssue(report, "duration.duration_ms", "greater than 0")) {
      Fail("missing actionable issue for duration.duration_ms");
    }
    if (!ContainsIssue(report, "camera.fps", "positive integer")) {
      Fail("missing actionable issue for camera.fps");
    }
    if (!ContainsIssue(report, "camera.trigger_mode", "must be one of")) {
      Fail("missing actionable issue for camera.trigger_mode");
    }
    if (!ContainsIssue(report, "camera.trigger_source", "must be one of")) {
      Fail("missing actionable issue for camera.trigger_source");
    }
    if (!ContainsIssue(report, "camera.trigger_activation", "must be one of")) {
      Fail("missing actionable issue for camera.trigger_activation");
    }
    if (!ContainsIssue(report, "camera.roi.y", "required")) {
      Fail("missing actionable issue for camera.roi.y");
    }
    if (!ContainsIssue(report, "sim_faults.drop_percent", "range [0,100]")) {
      Fail("missing actionable issue for sim_faults.drop_percent");
    }
    if (!ContainsIssue(report, "backend", "must be one of")) {
      Fail("missing actionable issue for backend enum");
    }
    if (!ContainsIssue(report, "device_selector", "non-empty value")) {
      Fail("missing actionable issue for device_selector clause value");
    }
    if (!ContainsIssue(report, "thresholds", "at least one threshold")) {
      Fail("missing actionable issue for thresholds object");
    }
    if (!ContainsIssue(report, "oaat.variables[0].path", "non-empty string")) {
      Fail("missing actionable issue for oaat variable path");
    }
    if (!ContainsIssue(report, "oaat.variables[0].values", "non-empty array")) {
      Fail("missing actionable issue for oaat variable values");
    }
  }

  {
    const std::string invalid_selector_backend_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "selector_wrong_backend",
  "backend": "sim",
  "device_selector": "serial:SN-1001",
  "duration": { "duration_ms": 1000 },
  "camera": { "fps": 20 },
  "thresholds": { "min_avg_fps": 1.0 }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(invalid_selector_backend_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for selector/backend mismatch: " + error);
    }
    if (report.valid) {
      Fail("expected selector/backend mismatch scenario to fail validation");
    }
    if (!ContainsIssue(report, "device_selector", "requires backend")) {
      Fail("missing actionable issue for device_selector backend requirement");
    }
  }

  {
    const std::string invalid_webcam_selector_backend_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "webcam_selector_wrong_backend",
  "backend": "sim",
  "duration": { "duration_ms": 1000 },
  "camera": { "fps": 20 },
  "webcam": {
    "device_selector": {
      "id": "cam-0"
    }
  },
  "thresholds": { "min_avg_fps": 1.0 }
}
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(invalid_webcam_selector_backend_json, report,
                                                 error)) {
      Fail("ValidateScenarioText failed unexpectedly for webcam selector/backend mismatch: " +
           error);
    }
    if (report.valid) {
      Fail("expected webcam selector/backend mismatch scenario to fail validation");
    }
    if (!ContainsIssue(report, "webcam.device_selector", "requires backend")) {
      Fail("missing actionable issue for webcam.device_selector backend requirement");
    }
  }

  {
    const std::string invalid_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "broken"
)json";

    labops::scenarios::ValidationReport report;
    std::string error;
    if (!labops::scenarios::ValidateScenarioText(invalid_json, report, error)) {
      Fail("ValidateScenarioText failed unexpectedly for parse-error json: " + error);
    }
    if (report.valid) {
      Fail("expected parse-error json to fail validation");
    }
    if (!ContainsIssue(report, "$", "parse error at line")) {
      Fail("missing actionable parse error message");
    }
  }

  std::cout << "scenario_validation_smoke: ok\n";
  return 0;
}
