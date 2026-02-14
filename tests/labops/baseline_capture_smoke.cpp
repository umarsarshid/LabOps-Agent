#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

fs::path ResolveScenarioPath(const std::string& scenario_name) {
  // Integration tests can execute from different working directories in
  // editors and CI. Probe a few common roots to keep scenario lookup stable.
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

void AssertNoRunIdSubdirectories(const fs::path& baseline_dir) {
  for (const auto& entry : fs::directory_iterator(baseline_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      Fail("baseline capture must write directly to baselines/<scenario_id>/");
    }
  }
}

} // namespace

int main() {
  const fs::path scenario_path = ResolveScenarioPath("sim_baseline.json");
  if (scenario_path.empty()) {
    Fail("unable to resolve scenarios/sim_baseline.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() / ("labops-baseline-capture-" + std::to_string(now_ms));

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temporary root");
  }

  const fs::path original_cwd = fs::current_path(ec);
  if (ec) {
    Fail("failed to resolve original cwd");
  }

  fs::current_path(temp_root, ec);
  if (ec) {
    Fail("failed to switch cwd for baseline capture test");
  }

  std::vector<std::string> argv_storage = {
      "labops",
      "baseline",
      "capture",
      scenario_path.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 0) {
    fs::current_path(original_cwd, ec);
    Fail("labops baseline capture returned non-zero exit code");
  }

  const fs::path baseline_dir = temp_root / "baselines" / "sim_baseline";
  if (!fs::exists(baseline_dir) || !fs::is_directory(baseline_dir)) {
    fs::current_path(original_cwd, ec);
    Fail("expected baseline directory was not created");
  }

  const fs::path scenario_json = baseline_dir / "scenario.json";
  const fs::path hostprobe_json = baseline_dir / "hostprobe.json";
  const fs::path run_json = baseline_dir / "run.json";
  const fs::path events_jsonl = baseline_dir / "events.jsonl";
  const fs::path metrics_csv = baseline_dir / "metrics.csv";
  const fs::path metrics_json = baseline_dir / "metrics.json";
  const fs::path summary_markdown = baseline_dir / "summary.md";
  const fs::path bundle_manifest_json = baseline_dir / "bundle_manifest.json";

  if (!fs::exists(scenario_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing scenario.json");
  }
  if (!fs::exists(hostprobe_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing hostprobe.json");
  }
  if (!fs::exists(run_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing run.json");
  }
  if (!fs::exists(events_jsonl)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing events.jsonl");
  }
  if (!fs::exists(metrics_csv)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing metrics.csv");
  }
  if (!fs::exists(metrics_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing metrics.json");
  }
  if (!fs::exists(summary_markdown)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing summary.md");
  }
  if (!fs::exists(bundle_manifest_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing bundle_manifest.json");
  }

  std::ifstream metrics_csv_input(metrics_csv, std::ios::binary);
  if (!metrics_csv_input) {
    fs::current_path(original_cwd, ec);
    Fail("failed to open baseline metrics.csv");
  }
  const std::string metrics_csv_content((std::istreambuf_iterator<char>(metrics_csv_input)),
                                        std::istreambuf_iterator<char>());
  AssertContains(metrics_csv_content, "avg_fps,");
  AssertContains(metrics_csv_content, "drop_rate_percent");

  std::ifstream metrics_json_input(metrics_json, std::ios::binary);
  if (!metrics_json_input) {
    fs::current_path(original_cwd, ec);
    Fail("failed to open baseline metrics.json");
  }
  const std::string metrics_json_content((std::istreambuf_iterator<char>(metrics_json_input)),
                                         std::istreambuf_iterator<char>());
  AssertContains(metrics_json_content, "\"avg_fps\":");
  AssertContains(metrics_json_content, "\"drop_rate_percent\":");

  AssertNoRunIdSubdirectories(baseline_dir);

  fs::current_path(original_cwd, ec);
  fs::remove_all(temp_root, ec);
  std::cout << "baseline_capture_smoke: ok\n";
  return 0;
}
