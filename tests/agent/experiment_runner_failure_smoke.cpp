#include "../common/assertions.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"
#include "agent/experiment_runner.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::AssertContains;
using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::Fail;
using labops::tests::common::RequireScenarioPath;

} // namespace

int main() {
  const fs::path baseline_scenario_path = RequireScenarioPath("sim_baseline.json");
  const fs::path temp_root = CreateUniqueTempDir("labops-agent-experiment-runner-failure");
  const fs::path output_root = temp_root / "agent-output";
  const fs::path missing_variant_path = temp_root / "missing_variant.json";

  std::error_code ec;

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
