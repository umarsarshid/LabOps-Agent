#include "../common/assertions.hpp"
#include "../common/run_fixtures.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::CollectRunBundleDirs;
using labops::tests::common::CountFilesWithPrefixAndExtension;
using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::Fail;
using labops::tests::common::RequireScenarioPath;
using labops::tests::common::RunScenarioOrFail;

void AssertBundleHasRequiredFiles(const fs::path& bundle_dir) {
  const fs::path run_json = bundle_dir / "run.json";
  const fs::path scenario_json = bundle_dir / "scenario.json";
  const fs::path hostprobe_json = bundle_dir / "hostprobe.json";
  const fs::path bundle_manifest_json = bundle_dir / "bundle_manifest.json";
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  const fs::path metrics_csv = bundle_dir / "metrics.csv";
  const fs::path metrics_json = bundle_dir / "metrics.json";
  const fs::path summary_markdown = bundle_dir / "summary.md";
  const fs::path report_html = bundle_dir / "report.html";

  if (!fs::exists(run_json)) {
    Fail("bundle missing run.json");
  }
  if (!fs::exists(scenario_json)) {
    Fail("bundle missing scenario.json");
  }
  if (!fs::exists(hostprobe_json)) {
    Fail("bundle missing hostprobe.json");
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
  if (!fs::exists(summary_markdown)) {
    Fail("bundle missing summary.md");
  }
  if (!fs::exists(report_html)) {
    Fail("bundle missing report.html");
  }
  if (CountFilesWithPrefixAndExtension(bundle_dir, "nic_", ".txt") == 0U) {
    Fail("bundle missing raw NIC command output files (nic_*.txt)");
  }
}

} // namespace

int main() {
  const fs::path scenario_path = RequireScenarioPath("sim_baseline.json");

  const fs::path root = CreateUniqueTempDir("labops-bundle-layout");
  const fs::path out_root = root / "out";

  std::error_code ec;
  RunScenarioOrFail(scenario_path, out_root);
  RunScenarioOrFail(scenario_path, out_root);

  const std::vector<fs::path> bundle_dirs = CollectRunBundleDirs(out_root);
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
