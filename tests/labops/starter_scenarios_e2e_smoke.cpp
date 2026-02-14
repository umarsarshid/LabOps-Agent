#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

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

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  if (!fs::exists(out_root)) {
    Fail("output root does not exist");
  }

  std::vector<fs::path> bundle_dirs;
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  if (bundle_dirs.size() != 1U) {
    Fail("expected exactly one run bundle directory");
  }
  return bundle_dirs.front();
}

fs::path ResolveScenarioPath(const std::string& scenario_name) {
  // The test can run from different working directories depending on IDE/CI.
  // Probe common roots so the scenario files remain discoverable.
  const std::vector<fs::path> roots = {
      fs::current_path(),
      fs::current_path() / "..",
      fs::current_path() / "../..",
  };

  for (const auto& root : roots) {
    const fs::path candidate = root / "scenarios" / scenario_name;
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
      return candidate;
    }
  }

  return {};
}

void RunScenarioE2E(const std::string& scenario_name, std::uint64_t run_suffix) {
  const fs::path scenario_path = ResolveScenarioPath(scenario_name);
  if (scenario_path.empty()) {
    Fail("unable to resolve scenario path for: " + scenario_name);
  }

  const fs::path out_dir =
      fs::temp_directory_path() / ("labops-starter-" + scenario_name + "-" + std::to_string(run_suffix));
  std::error_code ec;
  fs::remove_all(out_dir, ec);

  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_dir.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 0) {
    Fail("labops run returned non-zero exit code for scenario: " + scenario_name);
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  const fs::path run_json = bundle_dir / "run.json";
  const fs::path scenario_json = bundle_dir / "scenario.json";
  const fs::path bundle_manifest_json = bundle_dir / "bundle_manifest.json";
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  const fs::path metrics_csv = bundle_dir / "metrics.csv";
  const fs::path metrics_json = bundle_dir / "metrics.json";
  const fs::path summary_markdown = bundle_dir / "summary.md";
  if (!fs::exists(run_json)) {
    Fail("run.json missing for scenario: " + scenario_name);
  }
  if (!fs::exists(scenario_json)) {
    Fail("scenario.json missing for scenario: " + scenario_name);
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
