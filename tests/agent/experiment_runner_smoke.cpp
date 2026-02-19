#include "../common/assertions.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"
#include "agent/experiment_runner.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::AssertContains;
using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::Fail;
using labops::tests::common::ReadFileToString;
using labops::tests::common::RequireScenarioPath;

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

} // namespace

int main() {
  const fs::path baseline_scenario_path = RequireScenarioPath("sim_baseline.json");
  const fs::path variant_scenario_path = RequireScenarioPath("dropped_frames.json");
  const fs::path temp_root = CreateUniqueTempDir("labops-agent-experiment-runner");
  const fs::path output_root = temp_root / "agent-output";

  std::error_code ec;

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

  const std::string baseline_run_json = ReadFileToString(result.baseline_dir / "run.json");
  AssertContains(baseline_run_json, "\"scenario_id\":\"sim_baseline\"");

  const std::string variant_run_json = ReadFileToString(result.variant_bundle_dir / "run.json");
  AssertContains(variant_run_json, "\"scenario_id\":\"dropped_frames\"");

  fs::remove_all(temp_root, ec);
  std::cout << "experiment_runner_smoke: ok\n";
  return 0;
}
