#include "backends/real_sdk/real_backend_factory.hpp"
#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

fs::path MakeFixtureFile() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-list-devices-smoke-" + std::to_string(now_ms));

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp fixture directory");
  }

  const fs::path fixture_path = temp_root / "devices.csv";
  std::ofstream out(fixture_path, std::ios::binary);
  if (!out) {
    Fail("failed to open fixture file");
  }

  out << "model,serial,user_id,transport,ip,mac,firmware_version,sdk_version\n";
  out << "SprintCam,SN-1001,Primary,GigE,10.0.0.21,aa-bb-cc-dd-ee-01,3.2.1,21.1.8\n";
  out << "SprintCam,SN-1002,,USB3VISION,,,,\n";
  return fixture_path;
}

int DispatchWithCapturedStreams(std::vector<std::string> argv_storage, std::string& stdout_text,
                                std::string& stderr_text) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  std::ostringstream captured_stdout;
  std::ostringstream captured_stderr;
  std::streambuf* original_cout = std::cout.rdbuf(captured_stdout.rdbuf());
  std::streambuf* original_cerr = std::cerr.rdbuf(captured_stderr.rdbuf());
  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  std::cout.rdbuf(original_cout);
  std::cerr.rdbuf(original_cerr);

  stdout_text = captured_stdout.str();
  stderr_text = captured_stderr.str();
  return exit_code;
}

} // namespace

int main() {
  const fs::path fixture_path = MakeFixtureFile();
  const fs::path fixture_root = fixture_path.parent_path();
  const std::string fixture_path_text = fixture_path.string();
  ScopedEnvOverride fixture_override("LABOPS_REAL_DEVICE_FIXTURE", fixture_path_text.c_str());

  std::string stdout_text;
  std::string stderr_text;
  const int exit_code = DispatchWithCapturedStreams({"labops", "list-devices", "--backend", "real"},
                                                    stdout_text, stderr_text);

  if (labops::backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    if (exit_code != 0) {
      Fail("list-devices should succeed when real backend is enabled");
    }
    AssertContains(stdout_text, "backend: real");
    AssertContains(stdout_text, "status: enabled");
    AssertContains(stdout_text, "devices: 2");
    AssertContains(stdout_text, "device[0].model: SprintCam");
    AssertContains(stdout_text, "device[0].serial: SN-1001");
    AssertContains(stdout_text, "device[0].user_id: Primary");
    AssertContains(stdout_text, "device[0].transport: gige");
    AssertContains(stdout_text, "device[0].firmware_version: 3.2.1");
    AssertContains(stdout_text, "device[0].sdk_version: 21.1.8");
    AssertContains(stdout_text, "device[0].ip: 10.0.0.21");
    AssertContains(stdout_text, "device[0].mac: AA:BB:CC:DD:EE:01");
    AssertContains(stdout_text, "device[1].serial: SN-1002");
    AssertContains(stdout_text, "device[1].user_id: (none)");
    AssertContains(stdout_text, "device[1].transport: usb");
    std::error_code ec;
    fs::remove_all(fixture_root, ec);
    return 0;
  }

  if (exit_code == 0) {
    Fail("list-devices should fail when real backend is not available");
  }
  AssertContains(stderr_text, "BACKEND_NOT_AVAILABLE");
  AssertContains(stderr_text,
                 std::string("real backend ") +
                     std::string(labops::backends::real_sdk::RealBackendAvailabilityStatusText()));
  std::error_code ec;
  fs::remove_all(fixture_root, ec);
  return 0;
}
