#include "../common/assertions.hpp"
#include "../common/run_fixtures.hpp"
#include "../common/scenario_fixtures.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::CountFilesWithPrefixAndExtension;
using labops::tests::common::Fail;
using labops::tests::common::RequireScenarioPath;
using labops::tests::common::RequireSingleRunBundleDir;
using labops::tests::common::RunScenarioOrFail;

std::vector<std::string> ReadNonEmptyLines(const fs::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + file_path.string());
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

bool ContainsEventType(const std::vector<std::string>& lines, std::string_view event_type) {
  const std::string needle = "\"type\":\"" + std::string(event_type) + "\"";
  for (const auto& line : lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void RunScenarioE2E(const std::string& scenario_name, std::uint64_t run_suffix) {
  const fs::path scenario_path = RequireScenarioPath(scenario_name);

  const fs::path out_dir = fs::temp_directory_path() /
                           ("labops-starter-" + scenario_name + "-" + std::to_string(run_suffix));
  std::error_code ec;
  fs::remove_all(out_dir, ec);

  RunScenarioOrFail(scenario_path, out_dir, {},
                    std::string("labops run returned non-zero exit code for scenario: ") +
                        scenario_name);

  const fs::path bundle_dir = RequireSingleRunBundleDir(out_dir);
  const fs::path run_json = bundle_dir / "run.json";
  const fs::path scenario_json = bundle_dir / "scenario.json";
  const fs::path hostprobe_json = bundle_dir / "hostprobe.json";
  const fs::path bundle_manifest_json = bundle_dir / "bundle_manifest.json";
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  const fs::path metrics_csv = bundle_dir / "metrics.csv";
  const fs::path metrics_json = bundle_dir / "metrics.json";
  const fs::path summary_markdown = bundle_dir / "summary.md";
  const fs::path report_html = bundle_dir / "report.html";
  if (!fs::exists(run_json)) {
    Fail("run.json missing for scenario: " + scenario_name);
  }
  if (!fs::exists(scenario_json)) {
    Fail("scenario.json missing for scenario: " + scenario_name);
  }
  if (!fs::exists(hostprobe_json)) {
    Fail("hostprobe.json missing for scenario: " + scenario_name);
  }
  if (!fs::exists(bundle_manifest_json)) {
    Fail("bundle_manifest.json missing for scenario: " + scenario_name);
  }
  if (!fs::exists(events_jsonl)) {
    Fail("events.jsonl missing for scenario: " + scenario_name);
  }
  if (!fs::exists(metrics_csv)) {
    Fail("metrics.csv missing for scenario: " + scenario_name);
  }
  if (!fs::exists(metrics_json)) {
    Fail("metrics.json missing for scenario: " + scenario_name);
  }
  if (!fs::exists(summary_markdown)) {
    Fail("summary.md missing for scenario: " + scenario_name);
  }
  if (!fs::exists(report_html)) {
    Fail("report.html missing for scenario: " + scenario_name);
  }
  if (CountFilesWithPrefixAndExtension(bundle_dir, "nic_", ".txt") == 0U) {
    Fail("raw NIC command outputs missing for scenario: " + scenario_name);
  }

  const std::vector<std::string> lines = ReadNonEmptyLines(events_jsonl);
  if (lines.size() < 3U) {
    Fail("events trace too short for scenario: " + scenario_name);
  }
  if (!ContainsEventType(lines, "CONFIG_APPLIED")) {
    Fail("missing CONFIG_APPLIED in scenario: " + scenario_name);
  }
  if (!ContainsEventType(lines, "STREAM_STARTED")) {
    Fail("missing STREAM_STARTED in scenario: " + scenario_name);
  }
  if (!ContainsEventType(lines, "STREAM_STOPPED")) {
    Fail("missing STREAM_STOPPED in scenario: " + scenario_name);
  }

  fs::remove_all(out_dir, ec);
}

} // namespace

int main() {
  const std::vector<std::string> scenario_names = {
      "sim_baseline.json",
      "dropped_frames.json",
      "trigger_roi.json",
  };

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  std::uint64_t run_index = 0;
  for (const auto& scenario_name : scenario_names) {
    RunScenarioE2E(scenario_name, static_cast<std::uint64_t>(now_ms) + run_index);
    ++run_index;
  }

  std::cout << "starter_scenarios_e2e_smoke: ok\n";
  return 0;
}
