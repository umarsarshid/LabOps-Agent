#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/temp_dir.hpp"
#include "backends/real_sdk/real_backend_factory.hpp"
#include "core/errors/exit_codes.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::optional<std::string> ReadEnvVar(const char* name) {
  if (name == nullptr) {
    return std::nullopt;
  }
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

void SetEnvVar(const char* name, const char* value) {
#if defined(_WIN32)
  if (_putenv_s(name, value) != 0) {
    labops::tests::common::Fail("failed to set environment variable");
  }
#else
  if (setenv(name, value, 1) != 0) {
    labops::tests::common::Fail("failed to set environment variable");
  }
#endif
}

void UnsetEnvVar(const char* name) {
#if defined(_WIN32)
  if (_putenv_s(name, "") != 0) {
    labops::tests::common::Fail("failed to unset environment variable");
  }
#else
  if (unsetenv(name) != 0) {
    labops::tests::common::Fail("failed to unset environment variable");
  }
#endif
}

class ScopedEnvOverride {
public:
  ScopedEnvOverride(const char* name, const char* value)
      : name_(name), previous_(ReadEnvVar(name)) {
    SetEnvVar(name_, value);
  }

  ~ScopedEnvOverride() {
    if (previous_.has_value()) {
      SetEnvVar(name_, previous_->c_str());
      return;
    }
    UnsetEnvVar(name_);
  }

  ScopedEnvOverride(const ScopedEnvOverride&) = delete;
  ScopedEnvOverride& operator=(const ScopedEnvOverride&) = delete;

private:
  const char* name_ = "";
  std::optional<std::string> previous_;
};

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

void AssertFileExists(const fs::path& path, std::string_view label) {
  if (!fs::exists(path)) {
    labops::tests::common::Fail(std::string(label) + " missing: " + path.string());
  }
}

void WriteScenario(const fs::path& scenario_path) {
  std::ofstream scenario_file(scenario_path, std::ios::binary);
  if (!scenario_file) {
    labops::tests::common::Fail("failed to create scenario file");
  }

  scenario_file << "{\n"
                << "  \"schema_version\": \"1.0\",\n"
                << "  \"scenario_id\": \"run_reconnect_policy_smoke\",\n"
                << "  \"backend\": \"real_stub\",\n"
                << "  \"duration\": {\n"
                << "    \"duration_ms\": 5000\n"
                << "  },\n"
                << "  \"camera\": {\n"
                << "    \"fps\": 25\n"
                << "  },\n"
                << "  \"thresholds\": {\n"
                << "    \"min_avg_fps\": 1.0\n"
                << "  }\n"
                << "}\n";
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

  const fs::path temp_root = CreateUniqueTempDir("labops-run-reconnect-policy");
  const fs::path scenario_path = temp_root / "scenario_reconnect.json";
  const fs::path out_dir = temp_root / "out";
  WriteScenario(scenario_path);

  // This fixture hook triggers a deterministic mid-stream disconnect in the
  // OSS real backend implementation so reconnect policy behavior can be tested
  // without physically unplugging hardware.
  ScopedEnvOverride disconnect_override("LABOPS_REAL_DISCONNECT_AFTER_PULLS", "2");

  const int exit_code =
      DispatchArgs({"labops", "run", scenario_path.string(), "--out", out_dir.string()});
  if (exit_code != labops::core::errors::ToInt(labops::core::errors::ExitCode::kFailure)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected reconnect-exhausted run to return generic failure");
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
  AssertContains(events_text, "\"type\":\"DEVICE_DISCONNECTED\"");
  AssertContains(events_text, "\"type\":\"STREAM_STOPPED\"");
  AssertContains(events_text, "\"reason\":\"device_disconnect\"");
  AssertContains(events_text, "\"reconnect_retry_limit\":\"3\"");
  AssertContains(events_text, "\"reconnect_attempts_used_total\":\"3\"");

  const std::string summary_text = ReadFileToString(summary_md);
  AssertContains(summary_text, "device disconnected mid-run and reconnect attempts were exhausted");

  const std::string manifest_text = ReadFileToString(bundle_manifest);
  AssertContains(manifest_text, "\"path\":\"events.jsonl\"");
  AssertContains(manifest_text, "\"path\":\"metrics.csv\"");
  AssertContains(manifest_text, "\"path\":\"metrics.json\"");
  AssertContains(manifest_text, "\"path\":\"summary.md\"");
  AssertContains(manifest_text, "\"path\":\"report.html\"");

  RemovePathBestEffort(temp_root);
  return 0;
}
