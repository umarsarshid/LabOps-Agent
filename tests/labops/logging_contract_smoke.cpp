#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  if (!fs::exists(out_root)) {
    Fail("output root does not exist");
  }

  std::vector<fs::path> bundle_dirs;
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }

    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  if (bundle_dirs.size() != 1U) {
    Fail("expected exactly one run bundle directory");
  }

  return bundle_dirs.front();
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + path.string());
  }

  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

std::string ExtractRunIdFromRunJson(const std::string& run_json) {
  const std::string token = "\"run_id\":\"";
  const std::size_t start = run_json.find(token);
  if (start == std::string::npos) {
    return "";
  }

  const std::size_t value_start = start + token.size();
  const std::size_t value_end = run_json.find('"', value_start);
  if (value_end == std::string::npos || value_end == value_start) {
    return "";
  }

  return run_json.substr(value_start, value_end - value_start);
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() /
                             ("labops-logging-contract-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "scenario.json";
  const fs::path out_dir = temp_root / "out";

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
                  << "  \"scenario_id\": \"logging_contract\",\n"
                  << "  \"duration\": {\"duration_ms\": 600},\n"
                  << "  \"camera\": {\"fps\": 25, \"trigger_mode\": \"free_run\"},\n"
                  << "  \"sim_faults\": {\n"
                  << "    \"seed\": 42,\n"
                  << "    \"jitter_us\": 50,\n"
                  << "    \"drop_every_n\": 0,\n"
                  << "    \"drop_percent\": 0,\n"
                  << "    \"burst_drop\": 0,\n"
                  << "    \"reorder\": 0\n"
                  << "  },\n"
                  << "  \"thresholds\": {\"min_avg_fps\": 1}\n"
                  << "}\n";
  }

  std::string stderr_text;
  const int exit_code = DispatchWithCapturedStderr(
      {"labops", "run", scenario_path.string(), "--out", out_dir.string(), "--log-level", "debug"},
      stderr_text);
  if (exit_code != 0) {
    Fail("run command failed in logging contract test");
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  const std::string run_json = ReadFile(bundle_dir / "run.json");
  const std::string run_id = ExtractRunIdFromRunJson(run_json);
  if (run_id.empty()) {
    Fail("failed to extract run_id from run.json");
  }

  AssertContains(stderr_text, "level=INFO");
  AssertContains(stderr_text, "level=DEBUG");
  AssertContains(stderr_text, "msg=\"run initialized\"");
  AssertContains(stderr_text, std::string("run_id=\"") + run_id + "\"");

  fs::remove_all(temp_root, ec);
  std::cout << "logging_contract_smoke: ok\n";
  return 0;
}
