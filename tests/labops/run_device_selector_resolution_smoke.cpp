#include "backends/real_sdk/real_backend_factory.hpp"
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

void WriteFixtureCsv(const fs::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    Fail("failed to open fixture output file");
  }
  out << "model,serial,user_id,transport,ip,mac,firmware_version,sdk_version\n";
  out << "SprintCam,SN-1001,Primary,GigE,10.0.0.21,aa-bb-cc-dd-ee-01,3.2.1,21.1.8\n";
  out << "SprintCam,SN-2000,Secondary,USB3VISION,,,4.0.0,21.1.8\n";
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

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-run-device-selector-smoke-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "real_selector_run.json";
  const fs::path fixture_path = temp_root / "devices.csv";
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
                  << "  \"scenario_id\": \"run_device_selector_smoke\",\n"
                  << "  \"backend\": \"real_stub\",\n"
                  << "  \"device_selector\": \"serial:SN-1001\",\n"
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

  WriteFixtureCsv(fixture_path);
  const std::string fixture_path_text = fixture_path.string();
  ScopedEnvOverride fixture_override("LABOPS_REAL_DEVICE_FIXTURE", fixture_path_text.c_str());

  std::string stderr_output;
  const int exit_code =
      DispatchWithCapturedStderr({"labops", "run", scenario_path.string(), "--out",
                                  out_dir.string(), "--device", "serial:SN-2000"},
                                 stderr_output);

  if (labops::backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    if (exit_code !=
        labops::core::errors::ToInt(labops::core::errors::ExitCode::kBackendConnectFailed)) {
      Fail("expected backend-connect-failed exit code in real-enabled build");
    }
    AssertContains(stderr_output, "msg=\"device selector resolved\"");
    AssertContains(stderr_output, "selector=\"serial:SN-2000\"");
    AssertContains(stderr_output, "selected_serial=\"SN-2000\"");
    AssertContains(stderr_output, "selected_firmware_version=\"4.0.0\"");
    AssertContains(stderr_output, "selected_sdk_version=\"21.1.8\"");

    const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
    const fs::path run_json_path = bundle_dir / "run.json";
    if (!fs::exists(run_json_path)) {
      Fail("expected run.json to be written on backend connect failure");
    }
    const std::string run_json = ReadFile(run_json_path);
    AssertContains(run_json, "\"real_device\":");
    AssertContains(run_json, "\"model\":\"SprintCam\"");
    AssertContains(run_json, "\"serial\":\"SN-2000\"");
    AssertContains(run_json, "\"transport\":\"usb\"");
    AssertContains(run_json, "\"firmware_version\":\"4.0.0\"");
    AssertContains(run_json, "\"sdk_version\":\"21.1.8\"");
  } else {
    if (exit_code != labops::core::errors::ToInt(labops::core::errors::ExitCode::kFailure)) {
      Fail("expected generic failure exit code when real backend is disabled");
    }
    AssertContains(stderr_output, "device selector resolution failed");
    AssertContains(stderr_output, "real backend");
  }

  fs::remove_all(temp_root, ec);
  return 0;
}
