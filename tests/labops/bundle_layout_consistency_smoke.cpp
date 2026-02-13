#include "labops/cli/router.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

fs::path ResolveBaselineScenarioPath() {
  const std::vector<fs::path> roots = {
      fs::current_path(),
      fs::current_path() / "..",
      fs::current_path() / "../..",
  };

  for (const auto& root : roots) {
    const fs::path candidate = root / "scenarios" / "sim_baseline.json";
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
      return candidate;
    }
  }

  return {};
}

void RunScenario(const fs::path& scenario_path, const fs::path& out_root) {
  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_root.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 0) {
    Fail("labops run returned non-zero exit code");
  }
}

std::vector<fs::path> CollectBundleDirs(const fs::path& out_root) {
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

  std::sort(bundle_dirs.begin(), bundle_dirs.end());
  return bundle_dirs;
}

void AssertBundleHasRequiredFiles(const fs::path& bundle_dir) {
  const fs::path run_json = bundle_dir / "run.json";
  const fs::path scenario_json = bundle_dir / "scenario.json";
  const fs::path bundle_manifest_json = bundle_dir / "bundle_manifest.json";
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  const fs::path metrics_csv = bundle_dir / "metrics.csv";
  const fs::path metrics_json = bundle_dir / "metrics.json";

  if (!fs::exists(run_json)) {
    Fail("bundle missing run.json");
  }
  if (!fs::exists(scenario_json)) {
    Fail("bundle missing scenario.json");
  }
  if (!fs::exists(bundle_manifest_json)) {
    Fail("bundle missing bundle_manifest.json");
  }
  if (!fs::exists(events_jsonl)) {
    Fail("bundle missing events.jsonl");
  }
  if (!fs::exists(metrics_csv)) {
    Fail("bundle missing metrics.csv");
  }
  if (!fs::exists(metrics_json)) {
    Fail("bundle missing metrics.json");
  }
}

} // namespace

int main() {
  const fs::path scenario_path = ResolveBaselineScenarioPath();
  if (scenario_path.empty()) {
    Fail("unable to resolve scenarios/sim_baseline.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path root = fs::temp_directory_path() / ("labops-bundle-layout-" + std::to_string(now_ms));
  const fs::path out_root = root / "out";

  std::error_code ec;
  fs::remove_all(root, ec);

  RunScenario(scenario_path, out_root);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  RunScenario(scenario_path, out_root);

  const std::vector<fs::path> bundle_dirs = CollectBundleDirs(out_root);
  if (bundle_dirs.size() != 2U) {
    Fail("expected two run bundle directories after two runs");
  }
  if (bundle_dirs[0].filename() == bundle_dirs[1].filename()) {
    Fail("expected unique run bundle directory names");
  }

  for (const auto& bundle_dir : bundle_dirs) {
    AssertBundleHasRequiredFiles(bundle_dir);
  }

  fs::remove_all(root, ec);
  std::cout << "bundle_layout_consistency_smoke: ok\n";
  return 0;
}
