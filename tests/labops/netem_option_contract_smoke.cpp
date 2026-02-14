#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

int DispatchWithCapturedStderr(std::vector<std::string> argv_storage, std::string& stderr_text) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  std::ostringstream captured_cerr;
  std::streambuf* original_cerr = std::cerr.rdbuf(captured_cerr.rdbuf());
  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  std::cerr.rdbuf(original_cerr);
  stderr_text = captured_cerr.str();
  return exit_code;
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() / ("labops-netem-option-smoke-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "scenario.json";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    scenario_file << "{\n"
                  << "  \"schema_version\": \"1.0\",\n"
                  << "  \"scenario_id\": \"netem_contract\",\n"
                  << "  \"duration\": {\"duration_ms\": 1000},\n"
                  << "  \"camera\": {\"fps\": 30},\n"
                  << "  \"thresholds\": {\"min_avg_fps\": 1}\n"
                  << "}\n";
  }

  {
    std::string stderr_text;
    const int exit_code = DispatchWithCapturedStderr(
        {"labops", "run", scenario_path.string(), "--apply-netem"}, stderr_text);
    if (exit_code != 2) {
      Fail("expected usage exit code for --apply-netem without --netem-iface");
    }
    AssertContains(stderr_text, "--apply-netem requires --netem-iface");
  }

  {
    std::string stderr_text;
    const int exit_code = DispatchWithCapturedStderr(
        {"labops", "run", scenario_path.string(), "--netem-iface", "eth0"}, stderr_text);
    if (exit_code != 2) {
      Fail("expected usage exit code for --netem-iface without --apply-netem");
    }
    AssertContains(stderr_text, "--netem-iface requires --apply-netem");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "netem_option_contract_smoke: ok\n";
  return 0;
}
