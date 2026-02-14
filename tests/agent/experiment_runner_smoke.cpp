#include "agent/experiment_runner.hpp"

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

void AssertRequiredRunArtifacts(const fs::path& bundle_dir) {
  const std::vector<fs::path> required = {
      bundle_dir / "scenario.json", bundle_dir / "run.json",
      bundle_dir / "events.jsonl",  bundle_dir / "metrics.json",
      bundle_dir / "metrics.csv",   bundle_dir / "summary.md",
      bundle_dir / "report.html",   bundle_dir / "bundle_manifest.json",
  };

  for (const auto& artifact : required) {
    if (!fs::exists(artifact) || !fs::is_regular_file(artifact)) {
      Fail("missing required artifact: " + artifact.string());
    }
  }
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to read file: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  const fs::path baseline_scenario_path = ResolveScenarioPath("sim_baseline.json");
  if (baseline_scenario_path.empty()) {
    Fail("unable to resolve scenarios/sim_baseline.json");
  }

  const fs::path variant_scenario_path = ResolveScenarioPath("dropped_frames.json");
  if (variant_scenario_path.empty()) {
    Fail("unable to resolve scenarios/dropped_frames.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-agent-experiment-runner-" + std::to_string(now_ms));
  const fs::path output_root = temp_root / "agent-output";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  labops::agent::ExperimentRunRequest request;
  request.baseline_scenario_path = baseline_scenario_path.string();
  request.variant_scenario_path = variant_scenario_path.string();
  request.output_root = output_root;

  labops::agent::ExperimentRunner runner;
  labops::agent::ExperimentRunResult result;
  std::string error;
  if (!runner.RunBaselineAndVariant(request, result, error)) {
    Fail("ExperimentRunner failed: " + error);
  }

  if (result.baseline_run.run_id.empty()) {
    Fail("baseline run_id should not be empty");
  }
  if (result.variant_run.run_id.empty()) {
    Fail("variant run_id should not be empty");
  }

  if (!fs::exists(result.baseline_dir) || !fs::is_directory(result.baseline_dir)) {
    Fail("baseline output directory not found");
  }
  AssertRequiredRunArtifacts(result.baseline_dir);

  if (!fs::exists(result.variant_bundle_dir) || !fs::is_directory(result.variant_bundle_dir)) {
    Fail("variant bundle directory not found");
  }
  const std::string variant_bundle_name = result.variant_bundle_dir.filename().string();
  if (variant_bundle_name.rfind("run-", 0) != 0U) {
    Fail("variant bundle should use run-id directory naming");
  }
  AssertRequiredRunArtifacts(result.variant_bundle_dir);

  const std::string baseline_run_json = ReadFile(result.baseline_dir / "run.json");
  AssertContains(baseline_run_json, "\"scenario_id\":\"sim_baseline\"");

  const std::string variant_run_json = ReadFile(result.variant_bundle_dir / "run.json");
  AssertContains(variant_run_json, "\"scenario_id\":\"dropped_frames\"");

  fs::remove_all(temp_root, ec);
  std::cout << "experiment_runner_smoke: ok\n";
  return 0;
}
