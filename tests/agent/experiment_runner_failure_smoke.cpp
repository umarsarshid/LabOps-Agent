#include "agent/experiment_runner.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
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

} // namespace

int main() {
  const fs::path baseline_scenario_path = ResolveScenarioPath("sim_baseline.json");
  if (baseline_scenario_path.empty()) {
    Fail("unable to resolve scenarios/sim_baseline.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() /
                             ("labops-agent-experiment-runner-failure-" + std::to_string(now_ms));
  const fs::path output_root = temp_root / "agent-output";
  const fs::path missing_variant_path = temp_root / "missing_variant.json";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  labops::agent::ExperimentRunRequest request;
  request.baseline_scenario_path = baseline_scenario_path.string();
  request.variant_scenario_path = missing_variant_path.string();
  request.output_root = output_root;

  labops::agent::ExperimentRunner runner;
  labops::agent::ExperimentRunResult result;
  std::string error;
  if (runner.RunBaselineAndVariant(request, result, error)) {
    Fail("expected ExperimentRunner to fail for missing variant scenario path");
  }

  AssertContains(error, "variant scenario file not found");

  // Failure should occur before any run executes, so no output bundle should
  // be created when variant scenario path is invalid.
  if (fs::exists(output_root)) {
    Fail("output root should not be created when preflight validation fails");
  }

  if (!result.baseline_run.run_id.empty()) {
    Fail("baseline run should not start when preflight validation fails");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "experiment_runner_failure_smoke: ok\n";
  return 0;
}
