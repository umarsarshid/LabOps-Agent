#include "../common/assertions.hpp"
#include "../common/cli_dispatch.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using labops::tests::common::AssertContains;
using labops::tests::common::Fail;
using labops::tests::common::WriteFixtureFile;

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

int DispatchWithCapturedStreams(const std::vector<std::string>& args, std::string& stdout_text,
                                std::string& stderr_text) {
  std::ostringstream captured_stdout;
  std::ostringstream captured_stderr;
  std::streambuf* original_stdout = std::cout.rdbuf(captured_stdout.rdbuf());
  std::streambuf* original_stderr = std::cerr.rdbuf(captured_stderr.rdbuf());
  const int exit_code = labops::tests::common::DispatchArgs(args);
  std::cout.rdbuf(original_stdout);
  std::cerr.rdbuf(original_stderr);
  stdout_text = captured_stdout.str();
  stderr_text = captured_stderr.str();
  return exit_code;
}

} // namespace

int main() {
  const std::filesystem::path temp_root =
      labops::tests::common::CreateUniqueTempDir("labops-list-webcam-devices");
  const std::filesystem::path fixture_path = temp_root / "webcams.csv";
  WriteFixtureFile(fixture_path, "device_id,friendly_name,bus_info,capture_index\n"
                                 "cam-20,USB Camera 20,usb:2-1,20\n"
                                 "cam-10,USB Camera 10,usb:1-3,10\n");

  const std::string fixture_path_text = fixture_path.string();
  ScopedEnvOverride fixture_override("LABOPS_WEBCAM_DEVICE_FIXTURE", fixture_path_text.c_str());

  std::string stdout_text;
  std::string stderr_text;
  const int exit_code = DispatchWithCapturedStreams(
      {"labops", "list-devices", "--backend", "webcam"}, stdout_text, stderr_text);

  if (exit_code != 0) {
    Fail("webcam list-devices should succeed");
  }
  if (!stderr_text.empty()) {
    Fail("webcam list-devices should not emit stderr on success");
  }

  AssertContains(stdout_text, "backend: webcam");
  AssertContains(stdout_text, "status:");
  AssertContains(stdout_text, "devices: 2");
  AssertContains(stdout_text, "device[0].id: cam-10");
  AssertContains(stdout_text, "device[0].friendly_name: USB Camera 10");
  AssertContains(stdout_text, "device[0].bus_info: usb:1-3");
  AssertContains(stdout_text, "device[0].capture_index: 10");
  AssertContains(stdout_text, "device[1].id: cam-20");
  AssertContains(stdout_text, "device[1].friendly_name: USB Camera 20");

  labops::tests::common::RemovePathBestEffort(temp_root);
  return 0;
}
