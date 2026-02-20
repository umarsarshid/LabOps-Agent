#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/temp_dir.hpp"
#include "core/errors/exit_codes.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

class ScopedCurrentPath {
public:
  explicit ScopedCurrentPath(const fs::path& target) {
    std::error_code ec;
    original_path_ = fs::current_path(ec);
    if (ec) {
      labops::tests::common::Fail("failed to read current path");
    }
    fs::current_path(target, ec);
    if (ec) {
      labops::tests::common::Fail("failed to switch current path to lock-test root");
    }
  }

  ~ScopedCurrentPath() {
    std::error_code ec;
    fs::current_path(original_path_, ec);
  }

  ScopedCurrentPath(const ScopedCurrentPath&) = delete;
  ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

private:
  fs::path original_path_;
};

std::string CurrentPidText() {
#if defined(__linux__) || defined(__APPLE__)
  return std::to_string(static_cast<unsigned long>(::getpid()));
#else
  return "1";
#endif
}

void WriteWebcamScenario(const fs::path& scenario_path) {
  std::ofstream scenario_file(scenario_path, std::ios::binary);
  if (!scenario_file) {
    labops::tests::common::Fail("failed to create scenario file");
  }

  scenario_file << "{\n"
                << "  \"schema_version\": \"1.0\",\n"
                << "  \"scenario_id\": \"run_single_process_lock_smoke\",\n"
                << "  \"backend\": \"webcam\",\n"
                << "  \"duration\": {\n"
                << "    \"duration_ms\": 400\n"
                << "  },\n"
                << "  \"camera\": {\n"
                << "    \"fps\": 30\n"
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
  using labops::tests::common::RemovePathBestEffort;

  const fs::path temp_root = CreateUniqueTempDir("labops-run-lock-smoke");
  const fs::path scenario_path = temp_root / "run_lock_scenario.json";
  const fs::path out_dir = temp_root / "out";
  const fs::path lock_dir = temp_root / "tmp";
  const fs::path lock_path = lock_dir / "labops.lock";

  WriteWebcamScenario(scenario_path);

  std::error_code ec;
  fs::create_directories(lock_dir, ec);
  if (ec) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("failed to create lock directory");
  }

  {
    std::ofstream lock_file(lock_path, std::ios::binary | std::ios::trunc);
    if (!lock_file) {
      RemovePathBestEffort(temp_root);
      labops::tests::common::Fail("failed to write lock file");
    }
    lock_file << CurrentPidText() << '\n';
  }

  std::ostringstream captured_cerr;
  const int exit_code = [&]() {
    ScopedCurrentPath scoped_cwd(temp_root);
    std::streambuf* original_cerr = std::cerr.rdbuf(captured_cerr.rdbuf());
    const int dispatched_exit_code =
        DispatchArgs({"labops", "run", scenario_path.string(), "--out", out_dir.string()});
    std::cerr.rdbuf(original_cerr);
    return dispatched_exit_code;
  }();

  if (exit_code != labops::core::errors::ToInt(labops::core::errors::ExitCode::kFailure)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected run-lock conflict to return generic failure exit code");
  }

  const std::string stderr_output = captured_cerr.str();
  AssertContains(stderr_output, "another labops run appears active");
  AssertContains(stderr_output, "tmp/labops.lock");

  if (fs::exists(out_dir)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("lock conflict should fail before writing output bundles");
  }

  RemovePathBestEffort(temp_root);
  return 0;
}
