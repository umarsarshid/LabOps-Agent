#include "labops/cli/router.hpp"

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
      << "  labops run <scenario.json>\n"
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

int CommandRun(const std::vector<std::string_view>& args) {
  if (args.size() != 1) {
    std::cerr << "error: run requires exactly 1 argument: <scenario.json>\n";
    return kExitUsage;
  }

  std::string error;
  const std::string scenario_path(args.front());
  if (!ValidateScenarioPath(scenario_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  // Placeholder behavior until run execution/collection is implemented.
  std::cout << "run queued: " << scenario_path << '\n';
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
