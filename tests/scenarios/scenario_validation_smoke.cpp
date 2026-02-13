#include "scenarios/validator.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

bool ContainsIssue(const labops::scenarios::ValidationReport& report, std::string_view path,
                   std::string_view message_substring) {
  for (const auto& issue : report.issues) {
    if (issue.path == path &&
        issue.message.find(message_substring) != std::string::npos) {
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
    const std::string invalid_schema_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "Bad Id",
  "duration": { "duration_ms": 0 },
  "camera": {
    "fps": 0,
    "trigger_mode": "edge",
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
    if (!ContainsIssue(report, "camera.roi.y", "required")) {
      Fail("missing actionable issue for camera.roi.y");
    }
    if (!ContainsIssue(report, "sim_faults.drop_percent", "range [0,100]")) {
      Fail("missing actionable issue for sim_faults.drop_percent");
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
