#pragma once

#include "labops/cli/router.hpp"

#include <filesystem>
#include <string>

namespace labops::agent {

// Input contract for the first experiment-runner milestone:
// run one known-good baseline plus one variant scenario automatically.
struct ExperimentRunRequest {
  std::string baseline_scenario_path;
  std::string variant_scenario_path;
  std::filesystem::path output_root = "out-agent";
  bool redact_identifiers = false;
};

// Captures both run results so callers can inspect artifacts without parsing
// CLI stdout text.
struct ExperimentRunResult {
  labops::cli::ScenarioRunResult baseline_run;
  labops::cli::ScenarioRunResult variant_run;
  std::filesystem::path baseline_dir;
  std::filesystem::path variant_bundle_dir;
};

// Executes a simple two-step experiment plan in-process:
// 1) baseline capture
// 2) one variant run
//
// Returns true on full success. On failure, returns false and populates
// `error` with an actionable message.
class ExperimentRunner {
public:
  bool RunBaselineAndVariant(const ExperimentRunRequest& request, ExperimentRunResult& result,
                             std::string& error) const;
};

} // namespace labops::agent
