#include "scenarios/validator.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

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
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const fs::path temp_root =
        fs::temp_directory_path() / ("labops-netem-profile-smoke-" + std::to_string(now_ms));
    const fs::path tools_dir = temp_root / "tools" / "netem_profiles";
    const fs::path scenarios_dir = temp_root / "scenarios";
    const fs::path valid_scenario_path = scenarios_dir / "valid_with_profile.json";
    const fs::path invalid_scenario_path = scenarios_dir / "missing_profile.json";
    const fs::path profile_path = tools_dir / "jitter_light.json";

    std::error_code ec;
    fs::remove_all(temp_root, ec);
    fs::create_directories(tools_dir, ec);
    fs::create_directories(scenarios_dir, ec);
    if (ec) {
      Fail("failed to create temp paths for netem profile validation smoke");
    }

    {
      std::ofstream profile(profile_path, std::ios::binary);
      profile << "{\n"
              << "  \"profile_id\": \"jitter_light\",\n"
              << "  \"description\": \"smoke profile\",\n"
              << "  \"netem\": { \"delay_ms\": 5, \"jitter_ms\": 2, \"loss_percent\": 0, "
                 "\"reorder_percent\": 0 }\n"
              << "}\n";
    }

    {
      std::ofstream scenario(valid_scenario_path, std::ios::binary);
      scenario << "{\n"
               << "  \"schema_version\": \"1.0\",\n"
               << "  \"scenario_id\": \"valid_with_profile\",\n"
               << "  \"netem_profile\": \"jitter_light\",\n"
               << "  \"duration\": {\"duration_ms\": 1000},\n"
               << "  \"camera\": {\"fps\": 30},\n"
               << "  \"thresholds\": {\"min_avg_fps\": 10}\n"
               << "}\n";
    }

    {
      std::ofstream scenario(invalid_scenario_path, std::ios::binary);
      scenario << "{\n"
               << "  \"schema_version\": \"1.0\",\n"
               << "  \"scenario_id\": \"missing_profile\",\n"
               << "  \"netem_profile\": \"does_not_exist\",\n"
               << "  \"duration\": {\"duration_ms\": 1000},\n"
               << "  \"camera\": {\"fps\": 30},\n"
               << "  \"thresholds\": {\"min_avg_fps\": 10}\n"
               << "}\n";
    }

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
    const std::string invalid_schema_json = R"json(
{
  "schema_version": "1.0",
  "scenario_id": "Bad Id",
  "backend": "bad_backend",
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
    if (!ContainsIssue(report, "backend", "must be one of")) {
      Fail("missing actionable issue for backend enum");
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
