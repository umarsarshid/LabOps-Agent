#include "core/errors/exit_codes.hpp"
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
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-backend-connect-fail-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "backend_connect_fail_scenario.json";
  const fs::path out_dir = temp_root / "out";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  // The `real_stub` backend is always present in OSS builds and its connect()
  // path is intentionally not implemented, making this a deterministic
  // connection-failure fixture for exit-code validation.
  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    scenario_file << "{\n"
                  << "  \"schema_version\": \"1.0\",\n"
                  << "  \"scenario_id\": \"backend_connect_fail_smoke\",\n"
                  << "  \"backend\": \"real_stub\",\n"
                  << "  \"duration\": {\n"
                  << "    \"duration_ms\": 500\n"
                  << "  },\n"
                  << "  \"camera\": {\n"
                  << "    \"fps\": 30,\n"
                  << "    \"trigger_mode\": \"free_run\"\n"
                  << "  },\n"
                  << "  \"thresholds\": {\n"
                  << "    \"min_avg_fps\": 1.0\n"
                  << "  }\n"
                  << "}\n";
  }

  std::vector<std::string> argv_storage = {
      "labops", "run", scenario_path.string(), "--out", out_dir.string(),
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

  if (exit_code !=
      labops::core::errors::ToInt(labops::core::errors::ExitCode::kBackendConnectFailed)) {
    Fail("expected backend-connect-failed exit code");
  }

  const std::string stderr_output = captured_cerr.str();
  AssertContains(stderr_output, "backend connect failed");

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  if (!fs::exists(bundle_dir / "scenario.json")) {
    Fail("scenario.json missing for backend-connect-fail run");
  }
  if (!fs::exists(bundle_dir / "hostprobe.json")) {
    Fail("hostprobe.json missing for backend-connect-fail run");
  }
  if (!fs::exists(bundle_dir / "run.json")) {
    Fail("run.json missing for backend-connect-fail run");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "run_backend_connect_failure_smoke: ok\n";
  return 0;
}
