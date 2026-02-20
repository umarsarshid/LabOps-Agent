#include "core/errors/exit_codes.hpp"
#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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
    Fail("failed to set environment variable");
  }
#else
  if (setenv(name, value, 1) != 0) {
    Fail("failed to set environment variable");
  }
#endif
}

void UnsetEnvVar(const char* name) {
#if defined(_WIN32)
  if (_putenv_s(name, "") != 0) {
    Fail("failed to unset environment variable");
  }
#else
  if (unsetenv(name) != 0) {
    Fail("failed to unset environment variable");
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
    Fail("failed to open file");
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-webcam-selector-run-smoke-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "webcam_selector_run.json";
  const fs::path fixture_path = temp_root / "webcams.csv";
  const fs::path out_dir = temp_root / "out";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    if (!scenario_file) {
      Fail("failed to open scenario file");
    }
    scenario_file << "{\n"
                  << "  \"schema_version\": \"1.0\",\n"
                  << "  \"scenario_id\": \"webcam_selector_run\",\n"
                  << "  \"backend\": \"webcam\",\n"
                  << "  \"duration\": {\n"
                  << "    \"duration_ms\": 600\n"
                  << "  },\n"
                  << "  \"camera\": {\n"
                  << "    \"fps\": 30\n"
                  << "  },\n"
                  << "  \"webcam\": {\n"
                  << "    \"device_selector\": {\n"
                  << "      \"name_contains\": \"Camera 10\"\n"
                  << "    }\n"
                  << "  },\n"
                  << "  \"thresholds\": {\n"
                  << "    \"min_avg_fps\": 1.0\n"
                  << "  }\n"
                  << "}\n";
  }

  {
    std::ofstream fixture_file(fixture_path, std::ios::binary);
    if (!fixture_file) {
      Fail("failed to open webcam fixture file");
    }
    fixture_file << "device_id,friendly_name,bus_info,capture_index\n";
    // Use high capture indices to reduce accidental overlap with physical
    // cameras on developer machines.
    fixture_file << "cam-20,USB Camera 20,usb:2-1,9999\n";
    fixture_file << "cam-10,USB Camera 10,usb:1-3,9998\n";
  }

  const std::string fixture_path_text = fixture_path.string();
  ScopedEnvOverride fixture_override("LABOPS_WEBCAM_DEVICE_FIXTURE", fixture_path_text.c_str());

  std::string stderr_output;
  const int exit_code = DispatchWithCapturedStderr(
      {"labops", "run", scenario_path.string(), "--out", out_dir.string()}, stderr_output);

  const int success_exit_code =
      labops::core::errors::ToInt(labops::core::errors::ExitCode::kSuccess);
  const int backend_connect_failed_exit_code =
      labops::core::errors::ToInt(labops::core::errors::ExitCode::kBackendConnectFailed);
  if (exit_code != success_exit_code && exit_code != backend_connect_failed_exit_code) {
    Fail("expected webcam run to either succeed or fail with backend-connect-failed");
  }

  AssertContains(stderr_output, "msg=\"webcam device selector resolved\"");
  AssertContains(stderr_output, "selection_rule=\"name_contains\"");
  AssertContains(stderr_output, "selected_device_id=\"cam-10\"");
  AssertContains(stderr_output, "selected_friendly_name=\"USB Camera 10\"");

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  const fs::path run_json_path = bundle_dir / "run.json";
  if (!fs::exists(run_json_path)) {
    Fail("expected run.json to be written on webcam backend connect failure");
  }

  const std::string run_json = ReadFile(run_json_path);
  AssertContains(run_json, "\"webcam_device\":");
  AssertContains(run_json, "\"device_id\":\"cam-10\"");
  AssertContains(run_json, "\"friendly_name\":\"USB Camera 10\"");
  AssertContains(run_json, "\"bus_info\":\"usb:1-3\"");
  AssertContains(run_json, "\"selector\":\"name_contains:Camera 10\"");
  AssertContains(run_json, "\"selection_rule\":\"name_contains\"");
  AssertContains(run_json, "\"discovered_index\":0");

  fs::remove_all(temp_root, ec);
  return 0;
}
