#pragma once

#include "core/logging/logger.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace labops::cli {

// Shared run options for both CLI subcommands and internal C++ callers.
// Keeping one contract avoids behavior drift between `labops run` and agent-mode
// orchestrations that need the same artifact pipeline.
struct RunOptions {
  std::string scenario_path;
  std::filesystem::path output_dir = "out";
  bool zip_bundle = false;
  bool redact_identifiers = false;
  bool apply_netem = false;
  bool apply_netem_force = false;
  std::string netem_interface;
  core::logging::LogLevel log_level = core::logging::LogLevel::kInfo;
};

// Minimal run outputs needed by higher-level orchestrators to chain follow-up
// actions without shelling out to parse CLI text.
struct ScenarioRunResult {
  std::string run_id;
  std::filesystem::path bundle_dir;
  std::filesystem::path run_json_path;
  std::filesystem::path events_jsonl_path;
  std::filesystem::path metrics_json_path;
  bool thresholds_passed = false;
};

// Executes one scenario run through the same internal pipeline as `labops run`.
// This is intentionally exposed so agent-mode can orchestrate experiments
// directly in-process rather than invoking subprocess commands.
int ExecuteScenarioRun(const RunOptions& options, bool use_per_run_bundle_dir, bool allow_zip_bundle,
                       std::string_view success_prefix, ScenarioRunResult* run_result);

// Routes `labops` subcommands and returns process exit codes with a stable
// contract for scripts and CI:
//   0 => success
//   1 => command failed after valid invocation
//   2 => usage error (unknown command / invalid args)
int Dispatch(int argc, char** argv);

} // namespace labops::cli
