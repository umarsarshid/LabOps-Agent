#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
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

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() / ("labops-threshold-fail-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "threshold_fail_scenario.json";
  const fs::path out_dir = temp_root / "out";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  // This scenario should produce ~30 FPS. Threshold is intentionally strict to
  // guarantee a pass/fail violation for this smoke test.
  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    scenario_file << "{\n"
                  << "  \"schema_version\": \"1.0\",\n"
                  << "  \"scenario_id\": \"threshold_fail_smoke\",\n"
                  << "  \"duration\": {\n"
                  << "    \"duration_ms\": 1000\n"
                  << "  },\n"
                  << "  \"camera\": {\n"
                  << "    \"fps\": 30,\n"
                  << "    \"trigger_mode\": \"free_run\"\n"
                  << "  },\n"
                  << "  \"sim_faults\": {\n"
                  << "    \"seed\": 1,\n"
                  << "    \"jitter_us\": 0,\n"
                  << "    \"drop_every_n\": 0,\n"
                  << "    \"drop_percent\": 0,\n"
                  << "    \"burst_drop\": 0,\n"
                  << "    \"reorder\": 0\n"
                  << "  },\n"
                  << "  \"thresholds\": {\n"
                  << "    \"min_avg_fps\": 1000.0\n"
                  << "  }\n"
                  << "}\n";
  }

  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_dir.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 1) {
    Fail("expected labops run to return non-zero on threshold failure");
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  if (!fs::exists(bundle_dir / "run.json")) {
    Fail("run.json missing for threshold-fail run");
  }
  if (!fs::exists(bundle_dir / "events.jsonl")) {
    Fail("events.jsonl missing for threshold-fail run");
  }
  if (!fs::exists(bundle_dir / "metrics.csv")) {
    Fail("metrics.csv missing for threshold-fail run");
  }
  if (!fs::exists(bundle_dir / "metrics.json")) {
    Fail("metrics.json missing for threshold-fail run");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "run_threshold_failure_smoke: ok\n";
  return 0;
}
