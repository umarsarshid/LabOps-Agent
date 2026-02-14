#include "agent/experiment_runner.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace labops::agent {

namespace {

bool ValidateScenarioPath(const std::string& scenario_path, std::string_view label, std::string& error) {
  if (scenario_path.empty()) {
    error = std::string(label) + " scenario path cannot be empty";
    return false;
  }

  const fs::path path(scenario_path);
  std::error_code ec;
  if (!fs::exists(path, ec) || ec) {
    error = std::string(label) + " scenario file not found: " + scenario_path;
    return false;
  }

  if (!fs::is_regular_file(path, ec) || ec) {
    error = std::string(label) + " scenario path must point to a regular file: " + scenario_path;
    return false;
  }

  if (path.extension() != ".json") {
    error = std::string(label) + " scenario file must use .json extension: " + scenario_path;
    return false;
  }

  std::ifstream scenario_file(path);
  if (!scenario_file) {
    error = std::string(label) + " scenario file cannot be opened: " + scenario_path;
    return false;
  }

  return true;
}

bool ValidateRequest(const ExperimentRunRequest& request, std::string& error) {
  if (request.output_root.empty()) {
    error = "output root cannot be empty";
    return false;
  }
  if (!ValidateScenarioPath(request.baseline_scenario_path, "baseline", error)) {
    return false;
  }
  if (!ValidateScenarioPath(request.variant_scenario_path, "variant", error)) {
    return false;
  }
  return true;
}

std::string ScenarioIdFromPath(const std::string& scenario_path) {
  const std::string scenario_id = fs::path(scenario_path).stem().string();
  if (!scenario_id.empty()) {
    return scenario_id;
  }
  return "baseline";
}

} // namespace

bool ExperimentRunner::RunBaselineAndVariant(const ExperimentRunRequest& request,
                                             ExperimentRunResult& result,
                                             std::string& error) const {
  result = ExperimentRunResult{};
  error.clear();

  if (!ValidateRequest(request, error)) {
    return false;
  }

  // Capture baseline into a stable scenario-scoped directory so future compare
  // steps can reference a deterministic path.
  labops::cli::RunOptions baseline_options;
  baseline_options.scenario_path = request.baseline_scenario_path;
  baseline_options.output_dir = request.output_root / "baselines" /
                                ScenarioIdFromPath(request.baseline_scenario_path);
  baseline_options.zip_bundle = false;
  baseline_options.redact_identifiers = request.redact_identifiers;

  const int baseline_exit = labops::cli::ExecuteScenarioRun(baseline_options,
                                                            /*use_per_run_bundle_dir=*/false,
                                                            /*allow_zip_bundle=*/false,
                                                            "agent baseline captured: ",
                                                            &result.baseline_run);
  if (baseline_exit != 0) {
    error = "baseline run failed with exit code " + std::to_string(baseline_exit);
    return false;
  }
  result.baseline_dir = baseline_options.output_dir;

  // Execute exactly one variant against the same runner pipeline so artifact
  // semantics match normal `labops run` behavior.
  labops::cli::RunOptions variant_options;
  variant_options.scenario_path = request.variant_scenario_path;
  variant_options.output_dir = request.output_root / "runs";
  variant_options.zip_bundle = false;
  variant_options.redact_identifiers = request.redact_identifiers;

  const int variant_exit = labops::cli::ExecuteScenarioRun(variant_options,
                                                           /*use_per_run_bundle_dir=*/true,
                                                           /*allow_zip_bundle=*/true,
                                                           "agent variant queued: ",
                                                           &result.variant_run);
  if (variant_exit != 0) {
    error = "variant run failed with exit code " + std::to_string(variant_exit);
    return false;
  }
  result.variant_bundle_dir = result.variant_run.bundle_dir;

  return true;
}

} // namespace labops::agent
