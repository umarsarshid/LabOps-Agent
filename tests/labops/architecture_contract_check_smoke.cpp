#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/run_fixtures.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"
#include "core/errors/exit_codes.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> ReadNonEmptyLines(const fs::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    labops::tests::common::Fail("failed to open file: " + file_path.string());
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::size_t CountEventType(const std::vector<std::string>& lines, std::string_view event_type) {
  const std::string needle = "\"type\":\"" + std::string(event_type) + "\"";
  std::size_t count = 0;
  for (const auto& line : lines) {
    if (line.find(needle) != std::string::npos) {
      ++count;
    }
  }
  return count;
}

std::string FindFirstEventLine(const std::vector<std::string>& lines, std::string_view event_type) {
  const std::string needle = "\"type\":\"" + std::string(event_type) + "\"";
  for (const auto& line : lines) {
    if (line.find(needle) != std::string::npos) {
      return line;
    }
  }
  return {};
}

void AssertFileExists(const fs::path& path, std::string_view label) {
  if (!fs::exists(path)) {
    labops::tests::common::Fail(std::string(label) + " missing: " + path.string());
  }
}

void WritePassingScenario(const fs::path& path) {
  constexpr std::string_view kScenario = R"JSON(
{
  "schema_version": "1.0",
  "scenario_id": "architecture_contract_pass",
  "description": "Contract smoke fixture for stable run outputs.",
  "tags": ["contract", "smoke"],
  "backend": "sim",
  "duration": {
    "duration_ms": 800
  },
  "camera": {
    "device_id": "sim_cam_01",
    "pixel_format": "mono8",
    "width": 1280,
    "height": 720,
    "fps": 25,
    "trigger_mode": "free_run"
  },
  "sim_faults": {
    "seed": 42,
    "jitter_us": 0,
    "drop_every_n": 0,
    "drop_percent": 0,
    "burst_drop": 0,
    "reorder": 0
  },
  "thresholds": {
    "min_avg_fps": 1.0,
    "max_drop_rate_percent": 100.0
  }
}
)JSON";
  labops::tests::common::WriteScenarioFixture(path, kScenario);
}

void WriteThresholdFailScenario(const fs::path& path) {
  constexpr std::string_view kScenario = R"JSON(
{
  "schema_version": "1.0",
  "scenario_id": "architecture_contract_threshold_fail",
  "description": "Contract smoke fixture for threshold-fail exit-code semantics.",
  "tags": ["contract", "thresholds"],
  "backend": "sim",
  "duration": {
    "duration_ms": 800
  },
  "camera": {
    "device_id": "sim_cam_01",
    "pixel_format": "mono8",
    "width": 1280,
    "height": 720,
    "fps": 25,
    "trigger_mode": "free_run"
  },
  "sim_faults": {
    "seed": 777,
    "jitter_us": 0,
    "drop_every_n": 0,
    "drop_percent": 0,
    "burst_drop": 0,
    "reorder": 0
  },
  "thresholds": {
    "min_avg_fps": 1000.0
  }
}
)JSON";
  labops::tests::common::WriteScenarioFixture(path, kScenario);
}

void WriteInvalidScenario(const fs::path& path) {
  constexpr std::string_view kScenario = R"JSON(
{
  "schema_version": "1.0",
  "description": "Invalid contract fixture: missing scenario_id.",
  "tags": ["invalid", "contract"],
  "duration": {
    "duration_ms": 500
  },
  "camera": {
    "device_id": "sim_cam_01",
    "pixel_format": "mono8",
    "width": 640,
    "height": 480,
    "fps": 25,
    "trigger_mode": "free_run"
  },
  "sim_faults": {
    "seed": 1,
    "jitter_us": 0,
    "drop_every_n": 0,
    "drop_percent": 0,
    "burst_drop": 0,
    "reorder": 0
  },
  "thresholds": {
    "min_avg_fps": 1.0
  }
}
)JSON";
  labops::tests::common::WriteScenarioFixture(path, kScenario);
}

} // namespace

int main() {
  using labops::core::errors::ExitCode;
  using labops::core::errors::ToInt;
  using labops::tests::common::AssertContains;
  using labops::tests::common::CreateUniqueTempDir;
  using labops::tests::common::DispatchArgs;
  using labops::tests::common::DispatchRunScenario;
  using labops::tests::common::ReadFileToString;
  using labops::tests::common::RemovePathBestEffort;
  using labops::tests::common::RequireSingleRunBundleDir;

  const fs::path temp_root = CreateUniqueTempDir("labops-architecture-contract");
  const fs::path pass_scenario = temp_root / "scenario_pass.json";
  const fs::path fail_scenario = temp_root / "scenario_threshold_fail.json";
  const fs::path invalid_scenario = temp_root / "scenario_invalid.json";

  WritePassingScenario(pass_scenario);
  WriteThresholdFailScenario(fail_scenario);
  WriteInvalidScenario(invalid_scenario);

  // Invariant: a passing run exits with kSuccess and writes stable artifact names.
  const fs::path pass_out = temp_root / "out-pass";
  const int pass_exit = DispatchRunScenario(pass_scenario, pass_out);
  if (pass_exit != ToInt(ExitCode::kSuccess)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected passing run to return kSuccess");
  }

  const fs::path pass_bundle = RequireSingleRunBundleDir(pass_out);
  const fs::path run_json = pass_bundle / "run.json";
  const fs::path scenario_json = pass_bundle / "scenario.json";
  const fs::path events_jsonl = pass_bundle / "events.jsonl";
  const fs::path metrics_csv = pass_bundle / "metrics.csv";
  const fs::path metrics_json = pass_bundle / "metrics.json";
  const fs::path summary_md = pass_bundle / "summary.md";
  const fs::path report_html = pass_bundle / "report.html";
  const fs::path bundle_manifest = pass_bundle / "bundle_manifest.json";

  AssertFileExists(run_json, "run.json");
  AssertFileExists(scenario_json, "scenario.json");
  AssertFileExists(events_jsonl, "events.jsonl");
  AssertFileExists(metrics_csv, "metrics.csv");
  AssertFileExists(metrics_json, "metrics.json");
  AssertFileExists(summary_md, "summary.md");
  AssertFileExists(report_html, "report.html");
  AssertFileExists(bundle_manifest, "bundle_manifest.json");

  const std::string run_json_text = ReadFileToString(run_json);
  AssertContains(run_json_text, "\"run_id\":\"run-");
  AssertContains(run_json_text, "\"scenario_id\":\"");
  AssertContains(run_json_text, "\"backend\":\"sim\"");
  AssertContains(run_json_text, "\"duration_ms\":800");
  AssertContains(run_json_text, "\"timestamps\":{");

  const std::string metrics_json_text = ReadFileToString(metrics_json);
  AssertContains(metrics_json_text, "\"avg_fps\":");
  AssertContains(metrics_json_text, "\"frames_total\":");
  AssertContains(metrics_json_text, "\"drop_rate_percent\":");
  AssertContains(metrics_json_text, "\"inter_frame_interval_us\":");
  AssertContains(metrics_json_text, "\"inter_frame_jitter_us\":");

  const std::string summary_text = ReadFileToString(summary_md);
  AssertContains(summary_text, "**PASS**");

  const std::string manifest_text = ReadFileToString(bundle_manifest);
  AssertContains(manifest_text, "\"path\":\"scenario.json\"");
  AssertContains(manifest_text, "\"path\":\"run.json\"");
  AssertContains(manifest_text, "\"path\":\"events.jsonl\"");
  AssertContains(manifest_text, "\"path\":\"metrics.csv\"");
  AssertContains(manifest_text, "\"path\":\"metrics.json\"");
  AssertContains(manifest_text, "\"path\":\"summary.md\"");
  AssertContains(manifest_text, "\"path\":\"report.html\"");

  const std::vector<std::string> event_lines = ReadNonEmptyLines(events_jsonl);
  if (CountEventType(event_lines, "STREAM_STARTED") == 0U) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("missing STREAM_STARTED event");
  }
  if (CountEventType(event_lines, "STREAM_STOPPED") == 0U) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("missing STREAM_STOPPED event");
  }
  const std::string started_event = FindFirstEventLine(event_lines, "STREAM_STARTED");
  AssertContains(started_event, "\"run_id\":\"");
  AssertContains(started_event, "\"scenario_id\":\"");
  AssertContains(started_event, "\"backend\":\"sim\"");
  AssertContains(started_event, "\"duration_ms\":\"800\"");
  AssertContains(started_event, "\"fps\":\"25\"");
  AssertContains(started_event, "\"seed\":\"42\"");

  const std::string stopped_event = FindFirstEventLine(event_lines, "STREAM_STOPPED");
  AssertContains(stopped_event, "\"frames_total\":\"");
  AssertContains(stopped_event, "\"frames_received\":\"");
  AssertContains(stopped_event, "\"frames_dropped\":\"");

  // Invariant: threshold violations exit with kThresholdsFailed and still emit evidence.
  const fs::path fail_out = temp_root / "out-threshold-fail";
  const int fail_exit = DispatchRunScenario(fail_scenario, fail_out);
  if (fail_exit != ToInt(ExitCode::kThresholdsFailed)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected threshold-violating run to return kThresholdsFailed");
  }

  const fs::path fail_bundle = RequireSingleRunBundleDir(fail_out);
  AssertFileExists(fail_bundle / "run.json", "threshold-fail run.json");
  AssertFileExists(fail_bundle / "events.jsonl", "threshold-fail events.jsonl");
  AssertFileExists(fail_bundle / "metrics.json", "threshold-fail metrics.json");
  const std::string fail_summary = ReadFileToString(fail_bundle / "summary.md");
  AssertContains(fail_summary, "**FAIL**");
  AssertContains(fail_summary, "Threshold violations:");

  // Invariant: schema-invalid scenario validation exits with kSchemaInvalid.
  const int validate_exit = DispatchArgs({"labops", "validate", invalid_scenario.string()});
  if (validate_exit != ToInt(ExitCode::kSchemaInvalid)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected invalid scenario to return kSchemaInvalid");
  }

  RemovePathBestEffort(temp_root);
  return 0;
}
