#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
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

void WriteScenario(const fs::path& scenario_path) {
  std::ofstream scenario_file(scenario_path, std::ios::binary);
  if (!scenario_file) {
    labops::tests::common::Fail("failed to create scenario file");
  }

  scenario_file << "{\n"
                << "  \"schema_version\": \"1.0\",\n"
                << "  \"scenario_id\": \"sdk_log_capture_smoke\",\n"
                << "  \"backend\": \"real_stub\",\n"
                << "  \"duration\": {\n"
                << "    \"duration_ms\": 600\n"
                << "  },\n"
                << "  \"camera\": {\n"
                << "    \"fps\": 25\n"
                << "  },\n"
                << "  \"thresholds\": {\n"
                << "    \"min_avg_fps\": 1.0\n"
                << "  }\n"
                << "}\n";
}

void AssertCoreArtifactsExist(const fs::path& bundle_dir) {
  if (!fs::exists(bundle_dir / "scenario.json")) {
    labops::tests::common::Fail("scenario.json missing");
  }
  if (!fs::exists(bundle_dir / "hostprobe.json")) {
    labops::tests::common::Fail("hostprobe.json missing");
  }
  if (!fs::exists(bundle_dir / "run.json")) {
    labops::tests::common::Fail("run.json missing");
  }
}

} // namespace

int main() {
  using labops::tests::common::AssertContains;
  using labops::tests::common::CreateUniqueTempDir;
  using labops::tests::common::DispatchArgs;
  using labops::tests::common::ReadFileToString;
  using labops::tests::common::RemovePathBestEffort;

  const fs::path temp_root = CreateUniqueTempDir("labops-sdk-log-capture");
  const fs::path scenario_path = temp_root / "scenario.json";
  const fs::path out_without = temp_root / "out_without";
  const fs::path out_with = temp_root / "out_with";
  WriteScenario(scenario_path);

  const int exit_without_sdk_log =
      DispatchArgs({"labops", "run", scenario_path.string(), "--out", out_without.string()});
  const int exit_with_sdk_log = DispatchArgs(
      {"labops", "run", scenario_path.string(), "--out", out_with.string(), "--sdk-log"});

  // Optional capture must not alter run outcome; only evidence should differ.
  if (exit_without_sdk_log != exit_with_sdk_log) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected --sdk-log to preserve run exit behavior");
  }

  const fs::path bundle_without = ResolveSingleBundleDir(out_without);
  const fs::path bundle_with = ResolveSingleBundleDir(out_with);
  AssertCoreArtifactsExist(bundle_without);
  AssertCoreArtifactsExist(bundle_with);

  const fs::path sdk_log_without = bundle_without / "sdk_log.txt";
  if (fs::exists(sdk_log_without)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("sdk_log.txt should not exist when --sdk-log is omitted");
  }

  const fs::path sdk_log_with = bundle_with / "sdk_log.txt";
  if (!fs::exists(sdk_log_with)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("sdk_log.txt missing when --sdk-log is enabled");
  }
  const std::string sdk_log_text = ReadFileToString(sdk_log_with);
  AssertContains(sdk_log_text, "sdk_log_capture=enabled");

  RemovePathBestEffort(temp_root);
  return 0;
}
