#include "labops/cli/router.hpp"

#include "artifacts/run_writer.hpp"
#include "core/schema/run_contract.hpp"
#include "events/event_model.hpp"
#include "events/jsonl_writer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace labops::cli {

namespace {

// Exit code contract is centralized here so every subcommand remains
// consistent and automation can rely on deterministic behavior.
constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

// One usage text source avoids divergence between help and error paths.
void PrintUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops run <scenario.json> [--out <dir>]\n"
      << "  labops validate <scenario.json>\n"
      << "  labops version\n";
}

// Early-stage scenario preflight checks. These are intentionally lightweight
// and filesystem-focused; full schema validation belongs in the scenarios
// module as that gets implemented.
bool ValidateScenarioPath(const std::string& scenario_path, std::string& error) {
  if (scenario_path.empty()) {
    error = "scenario path cannot be empty";
    return false;
  }

  fs::path path(scenario_path);
  std::error_code ec;

  if (!fs::exists(path, ec) || ec) {
    error = "scenario file not found: " + scenario_path;
    return false;
  }

  if (!fs::is_regular_file(path, ec) || ec) {
    error = "scenario path must point to a regular file: " + scenario_path;
    return false;
  }

  if (path.extension() != ".json") {
    error = "scenario file must use .json extension: " + scenario_path;
    return false;
  }

  std::ifstream file(path);
  if (!file) {
    error = "unable to open scenario file: " + scenario_path;
    return false;
  }

  if (file.peek() == std::ifstream::traits_type::eof()) {
    error = "scenario file is empty: " + scenario_path;
    return false;
  }

  return true;
}

int CommandVersion(const std::vector<std::string_view>& args) {
  if (!args.empty()) {
    std::cerr << "error: version does not accept arguments\n";
    return kExitUsage;
  }

  std::cout << "labops 0.1.0\n";
  return kExitSuccess;
}

int CommandValidate(const std::vector<std::string_view>& args) {
  if (args.size() != 1) {
    std::cerr << "error: validate requires exactly 1 argument: <scenario.json>\n";
    return kExitUsage;
  }

  std::string error;
  const std::string scenario_path(args.front());
  if (!ValidateScenarioPath(scenario_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  std::cout << "valid: " << scenario_path << '\n';
  return kExitSuccess;
}

struct RunOptions {
  std::string scenario_path;
  fs::path output_dir = "out";
};

// Parse `run` args with an explicit contract:
// - one scenario path
// - optional `--out <dir>`
// Any unknown flags or duplicate positional args are treated as usage errors.
bool ParseRunOptions(const std::vector<std::string_view>& args, RunOptions& options,
                     std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--out") {
      if (i + 1 >= args.size()) {
        error = "missing value for --out";
        return false;
      }
      options.output_dir = fs::path(args[i + 1]);
      ++i;
      continue;
    }

    if (!token.empty() && token.front() == '-') {
      error = "unknown option: " + std::string(token);
      return false;
    }

    if (!options.scenario_path.empty()) {
      error = "run accepts exactly 1 scenario path";
      return false;
    }

    options.scenario_path = std::string(token);
  }

  if (options.scenario_path.empty()) {
    error = "run requires exactly 1 argument: <scenario.json>";
    return false;
  }

  return true;
}

// Generate a stable-enough run identifier for early artifact wiring. This is
// intentionally simple and timestamp-based until a dedicated ID module exists.
std::string MakeRunId(std::chrono::system_clock::time_point now) {
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return "run-" + std::to_string(millis);
}

// Build minimal run metadata with required fields so run.json is useful from
// day one, even before scenario parsing and runtime execution are implemented.
core::schema::RunInfo BuildRunInfo(const RunOptions& options,
                                   std::chrono::system_clock::time_point now) {
  core::schema::RunInfo run_info;
  run_info.run_id = MakeRunId(now);
  run_info.config.scenario_id = fs::path(options.scenario_path).stem().string();
  run_info.config.backend = "sim";
  run_info.config.seed = 0;
  run_info.config.duration = std::chrono::milliseconds(0);
  run_info.timestamps.created_at = now;
  run_info.timestamps.started_at = now;
  run_info.timestamps.finished_at = now;
  return run_info;
}

// Emit a minimal event immediately so every run has at least one timeline
// record. This validates the end-to-end event pipeline before real stream
// events are wired in.
events::Event BuildSampleEvent(const core::schema::RunInfo& run_info,
                               std::chrono::system_clock::time_point now) {
  events::Event event;
  event.ts = now;
  event.type = events::EventType::kRunStarted;
  event.payload = {
      {"run_id", run_info.run_id},
      {"scenario_id", run_info.config.scenario_id},
      {"backend", run_info.config.backend},
      {"note", "sample_event"},
  };
  return event;
}

int CommandRun(const std::vector<std::string_view>& args) {
  RunOptions options;
  std::string error;
  if (!ParseRunOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  if (!ValidateScenarioPath(options.scenario_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  const auto now = std::chrono::system_clock::now();
  const core::schema::RunInfo run_info = BuildRunInfo(options, now);

  fs::path written_path;
  if (!artifacts::WriteRunJson(run_info, options.output_dir, written_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  const events::Event sample_event = BuildSampleEvent(run_info, now);
  fs::path events_path;
  if (!events::AppendEventJsonl(sample_event, options.output_dir, events_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  std::cout << "run queued: " << options.scenario_path << '\n';
  std::cout << "artifact: " << written_path.string() << '\n';
  std::cout << "events: " << events_path.string() << '\n';
  return kExitSuccess;
}

} // namespace

int Dispatch(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(std::cerr);
    return kExitUsage;
  }

  const std::string_view command(argv[1]);
  const std::vector<std::string_view> args(argv + 2, argv + argc);

  // Explicit command dispatch keeps behavior obvious and easy to evolve while
  // command count is still small.
  if (command == "version") {
    return CommandVersion(args);
  }

  if (command == "validate") {
    return CommandValidate(args);
  }

  if (command == "run") {
    return CommandRun(args);
  }

  if (command == "help" || command == "--help" || command == "-h") {
    PrintUsage(std::cout);
    return kExitSuccess;
  }

  std::cerr << "error: unknown subcommand: " << command << '\n';
  PrintUsage(std::cerr);
  return kExitUsage;
}

} // namespace labops::cli
