#include "backends/real_sdk/real_backend_factory.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
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

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

void WriteFixtureCsv(const fs::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    Fail("failed to open fixture output file");
  }
  out << "# model,serial,user_id,transport,ip,mac,firmware_version,sdk_version\n";
  out << "model,serial,user_id,transport,ip,mac,firmware_version,sdk_version\n";
  out << "acA1920-40gm,SN-001,LineCamA,Gig E,192.168.10.11,aa-bb-cc-dd-ee-ff,3.2.1,21.1.8\n";
  out << "VisionPro ,, ,USB3VISION,,,,\n";
}

} // namespace

int main() {
  using labops::backends::real_sdk::DeviceInfo;
  using labops::backends::real_sdk::EnumerateConnectedDevices;
  using labops::backends::real_sdk::IsRealBackendEnabledAtBuild;

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-real-device-enumeration-" + std::to_string(now_ms));
  const fs::path fixture_path = temp_root / "devices.csv";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }
  WriteFixtureCsv(fixture_path);
  const std::string fixture_path_text = fixture_path.string();
  ScopedEnvOverride fixture_override("LABOPS_REAL_DEVICE_FIXTURE", fixture_path_text.c_str());

  std::vector<DeviceInfo> devices;
  std::string error;
  const bool ok = EnumerateConnectedDevices(devices, error);

  if (IsRealBackendEnabledAtBuild()) {
    if (!ok) {
      Fail("expected device enumeration to succeed when real backend is enabled");
    }
    if (devices.size() != 2U) {
      Fail("expected two devices from fixture");
    }

    if (devices[0].model != "acA1920-40gm" || devices[0].serial != "SN-001" ||
        devices[0].user_id != "LineCamA" || devices[0].transport != "gige") {
      Fail("unexpected normalized fields for first device");
    }
    if (!devices[0].ip_address.has_value() || devices[0].ip_address.value() != "192.168.10.11") {
      Fail("expected first device IP address");
    }
    if (!devices[0].mac_address.has_value() ||
        devices[0].mac_address.value() != "AA:BB:CC:DD:EE:FF") {
      Fail("expected normalized first device MAC address");
    }
    if (!devices[0].firmware_version.has_value() ||
        devices[0].firmware_version.value() != "3.2.1") {
      Fail("expected first device firmware version");
    }
    if (!devices[0].sdk_version.has_value() || devices[0].sdk_version.value() != "21.1.8") {
      Fail("expected first device sdk version");
    }

    if (devices[1].model != "VisionPro" || devices[1].serial != "unknown_serial" ||
        devices[1].transport != "usb") {
      Fail("unexpected normalized fields for second device");
    }
    if (devices[1].ip_address.has_value() || devices[1].mac_address.has_value() ||
        devices[1].firmware_version.has_value() || devices[1].sdk_version.has_value()) {
      Fail("expected empty optional fields for second device");
    }
  } else {
    if (ok) {
      Fail("expected enumeration to fail when real backend is unavailable");
    }
    AssertContains(error, "real backend");
  }

  fs::remove_all(temp_root, ec);
  return 0;
}
