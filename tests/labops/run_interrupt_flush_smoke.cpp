#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/temp_dir.hpp"
#include "backends/real_sdk/real_backend_factory.hpp"
#include "core/errors/exit_codes.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  if (!fs::exists(out_root)) {
    labops::tests::common::Fail("output root does not exist");
  }

  std::vector<fs::path> bundle_dirs;
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0U) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  if (bundle_dirs.size() != 1U) {
    labops::tests::common::Fail("expected exactly one run bundle directory");
  }
  return bundle_dirs.front();
}

void WriteInterruptScenario(const fs::path& scenario_path) {
  std::ofstream scenario_file(scenario_path, std::ios::binary);
  if (!scenario_file) {
    labops::tests::common::Fail("failed to create scenario file");
  }

  // Use a long duration to keep the run active long enough for the test thread
  // to deliver SIGINT after the CLI installs its handler.
  scenario_file << "{\n"
                << "  \"schema_version\": \"1.0\",\n"
                << "  \"scenario_id\": \"run_interrupt_flush_smoke\",\n"
                << "  \"backend\": \"real_stub\",\n"
                << "  \"duration\": {\n"
                << "    \"duration_ms\": 120000\n"
                << "  },\n"
                << "  \"camera\": {\n"
                << "    \"fps\": 30\n"
                << "  },\n"
                << "  \"thresholds\": {\n"
                << "    \"min_avg_fps\": 1.0\n"
                << "  }\n"
                << "}\n";
}

void AssertFileExists(const fs::path& path, std::string_view label) {
  if (!fs::exists(path)) {
    labops::tests::common::Fail(std::string(label) + " missing: " + path.string());
  }
}

} // namespace

int main() {
  using labops::tests::common::AssertContains;
  using labops::tests::common::CreateUniqueTempDir;
  using labops::tests::common::DispatchArgs;
  using labops::tests::common::ReadFileToString;
  using labops::tests::common::RemovePathBestEffort;

  if (!labops::backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    return 0;
  }

  const fs::path temp_root = CreateUniqueTempDir("labops-run-interrupt-flush");
  const fs::path scenario_path = temp_root / "scenario_interrupt.json";
  const fs::path out_dir = temp_root / "out";
  WriteInterruptScenario(scenario_path);

  std::atomic<bool> run_finished{false};
  std::atomic<bool> signal_sent{false};
  std::thread interrupter([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    if (run_finished.load()) {
      return;
    }

    signal_sent.store(true);
    std::raise(SIGINT);
  });

  const int exit_code =
      DispatchArgs({"labops", "run", scenario_path.string(), "--out", out_dir.string()});
  run_finished.store(true);
  interrupter.join();

  if (!signal_sent.load()) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("test precondition failed: SIGINT was not sent");
  }

  if (exit_code != labops::core::errors::ToInt(labops::core::errors::ExitCode::kFailure)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected interrupted run to return generic failure exit code");
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  const fs::path scenario_json = bundle_dir / "scenario.json";
  const fs::path hostprobe_json = bundle_dir / "hostprobe.json";
  const fs::path run_json = bundle_dir / "run.json";
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  const fs::path metrics_csv = bundle_dir / "metrics.csv";
  const fs::path metrics_json = bundle_dir / "metrics.json";
  const fs::path summary_md = bundle_dir / "summary.md";
  const fs::path report_html = bundle_dir / "report.html";
  const fs::path bundle_manifest = bundle_dir / "bundle_manifest.json";

  AssertFileExists(scenario_json, "scenario.json");
  AssertFileExists(hostprobe_json, "hostprobe.json");
  AssertFileExists(run_json, "run.json");
  AssertFileExists(events_jsonl, "events.jsonl");
  AssertFileExists(metrics_csv, "metrics.csv");
  AssertFileExists(metrics_json, "metrics.json");
  AssertFileExists(summary_md, "summary.md");
  AssertFileExists(report_html, "report.html");
  AssertFileExists(bundle_manifest, "bundle_manifest.json");

  const std::string events_text = ReadFileToString(events_jsonl);
  AssertContains(events_text, "\"type\":\"STREAM_STOPPED\"");
  AssertContains(events_text, "\"reason\":\"signal_interrupt\"");

  const std::string summary_text = ReadFileToString(summary_md);
  AssertContains(summary_text, "run interrupted by signal before requested duration completed");

  const std::string manifest_text = ReadFileToString(bundle_manifest);
  AssertContains(manifest_text, "\"path\":\"events.jsonl\"");
  AssertContains(manifest_text, "\"path\":\"metrics.csv\"");
  AssertContains(manifest_text, "\"path\":\"metrics.json\"");
  AssertContains(manifest_text, "\"path\":\"summary.md\"");
  AssertContains(manifest_text, "\"path\":\"report.html\"");

  RemovePathBestEffort(temp_root);
  return 0;
}
