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

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() / ("labops-validate-smoke-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "invalid.json";

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
                  << "  \"scenario_id\": \"Bad Id\",\n"
                  << "  \"duration\": {\"duration_ms\": 0},\n"
                  << "  \"camera\": {\"fps\": 0, \"trigger_mode\": \"edge\"},\n"
                  << "  \"sim_faults\": {\"drop_percent\": 150},\n"
                  << "  \"thresholds\": {}\n"
                  << "}\n";
  }

  std::vector<std::string> argv_storage = {
      "labops",
      "validate",
      scenario_path.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  std::ostringstream captured_cerr;
  std::streambuf* original_cerr = std::cerr.rdbuf(captured_cerr.rdbuf());
  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  std::cerr.rdbuf(original_cerr);

  if (exit_code == 0) {
    Fail("validate should fail for invalid scenario");
  }

  const std::string output = captured_cerr.str();
  AssertContains(output, "invalid scenario:");
  AssertContains(output, "scenario_id:");
  AssertContains(output, "duration.duration_ms:");
  AssertContains(output, "camera.fps:");
  AssertContains(output, "camera.trigger_mode:");
  AssertContains(output, "sim_faults.drop_percent:");
  AssertContains(output, "thresholds:");

  fs::remove_all(temp_root, ec);
  std::cout << "validate_actionable_smoke: ok\n";
  return 0;
}
