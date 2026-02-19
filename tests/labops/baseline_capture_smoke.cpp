#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/run_fixtures.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::AssertContains;
using labops::tests::common::CollectFilesWithPrefixAndExtension;
using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::DispatchArgs;
using labops::tests::common::Fail;
using labops::tests::common::ReadFileToString;
using labops::tests::common::RequireScenarioPath;

void AssertNoRunIdSubdirectories(const fs::path& baseline_dir) {
  for (const auto& entry : fs::directory_iterator(baseline_dir)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      Fail("baseline capture must write directly to baselines/<scenario_id>/");
    }
  }
}

} // namespace

int main() {
  const fs::path scenario_path = RequireScenarioPath("sim_baseline.json");
  const fs::path temp_root = CreateUniqueTempDir("labops-baseline-capture");

  std::error_code ec;

  const fs::path original_cwd = fs::current_path(ec);
  if (ec) {
    Fail("failed to resolve original cwd");
  }

  fs::current_path(temp_root, ec);
  if (ec) {
    Fail("failed to switch cwd for baseline capture test");
  }

  const int exit_code = DispatchArgs({"labops", "baseline", "capture", scenario_path.string()});
  if (exit_code != 0) {
    fs::current_path(original_cwd, ec);
    Fail("labops baseline capture returned non-zero exit code");
  }

  const fs::path baseline_dir = temp_root / "baselines" / "sim_baseline";
  if (!fs::exists(baseline_dir) || !fs::is_directory(baseline_dir)) {
    fs::current_path(original_cwd, ec);
    Fail("expected baseline directory was not created");
  }

  const fs::path scenario_json = baseline_dir / "scenario.json";
  const fs::path hostprobe_json = baseline_dir / "hostprobe.json";
  const fs::path run_json = baseline_dir / "run.json";
  const fs::path events_jsonl = baseline_dir / "events.jsonl";
  const fs::path metrics_csv = baseline_dir / "metrics.csv";
  const fs::path metrics_json = baseline_dir / "metrics.json";
  const fs::path summary_markdown = baseline_dir / "summary.md";
  const fs::path report_html = baseline_dir / "report.html";
  const fs::path bundle_manifest_json = baseline_dir / "bundle_manifest.json";

  if (!fs::exists(scenario_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing scenario.json");
  }
  if (!fs::exists(hostprobe_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing hostprobe.json");
  }
  if (!fs::exists(run_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing run.json");
  }
  if (!fs::exists(events_jsonl)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing events.jsonl");
  }
  if (!fs::exists(metrics_csv)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing metrics.csv");
  }
  if (!fs::exists(metrics_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing metrics.json");
  }
  if (!fs::exists(summary_markdown)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing summary.md");
  }
  if (!fs::exists(report_html)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing report.html");
  }
  if (!fs::exists(bundle_manifest_json)) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing bundle_manifest.json");
  }
  if (CollectFilesWithPrefixAndExtension(baseline_dir, "nic_", ".txt").empty()) {
    fs::current_path(original_cwd, ec);
    Fail("baseline missing raw NIC command output files (nic_*.txt)");
  }

  const std::string metrics_csv_content = ReadFileToString(metrics_csv);
  AssertContains(metrics_csv_content, "avg_fps,");
  AssertContains(metrics_csv_content, "drop_rate_percent");

  const std::string metrics_json_content = ReadFileToString(metrics_json);
  AssertContains(metrics_json_content, "\"avg_fps\":");
  AssertContains(metrics_json_content, "\"drop_rate_percent\":");

  AssertNoRunIdSubdirectories(baseline_dir);

  fs::current_path(original_cwd, ec);
  fs::remove_all(temp_root, ec);
  std::cout << "baseline_capture_smoke: ok\n";
  return 0;
}
