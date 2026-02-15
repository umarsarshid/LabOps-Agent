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
  out << "SprintCam,SN-1001,Primary,GigE,10.0.0.21,aa-bb-cc-dd-ee-01,1.0.0,21.1.8\n";
  out << "SprintCam,SN-1001,Primary,GigE,10.0.0.22,aa-bb-cc-dd-ee-02,1.0.1,21.1.8\n";
  out << "SprintCam,SN-2000,Secondary,USB3VISION,,,2.4.0,21.1.8\n";
}

} // namespace

int main() {
  using labops::backends::real_sdk::DeviceInfo;
  using labops::backends::real_sdk::DeviceSelector;
  using labops::backends::real_sdk::IsRealBackendEnabledAtBuild;
  using labops::backends::real_sdk::ParseDeviceSelector;
  using labops::backends::real_sdk::ResolveConnectedDevice;
  using labops::backends::real_sdk::ResolveDeviceSelector;

  std::vector<DeviceInfo> devices = {
      {
          .model = "SprintCam",
          .serial = "SN-1001",
          .user_id = "Primary",
          .transport = "gige",
          .ip_address = std::optional<std::string>("10.0.0.21"),
          .mac_address = std::optional<std::string>("AA:BB:CC:DD:EE:01"),
          .firmware_version = std::optional<std::string>("1.0.0"),
          .sdk_version = std::optional<std::string>("21.1.8"),
      },
      {
          .model = "SprintCam",
          .serial = "SN-1001",
          .user_id = "Primary",
          .transport = "gige",
          .ip_address = std::optional<std::string>("10.0.0.22"),
          .mac_address = std::optional<std::string>("AA:BB:CC:DD:EE:02"),
          .firmware_version = std::optional<std::string>("1.0.1"),
          .sdk_version = std::optional<std::string>("21.1.8"),
      },
      {
          .model = "SprintCam",
          .serial = "SN-2000",
          .user_id = "Secondary",
          .transport = "usb",
          .firmware_version = std::optional<std::string>("2.4.0"),
          .sdk_version = std::optional<std::string>("21.1.8"),
      },
  };

  {
    DeviceSelector selector;
    std::string error;
    if (!ParseDeviceSelector("serial:SN-2000", selector, error)) {
      Fail("expected serial selector to parse");
    }
    DeviceInfo selected;
    std::size_t selected_index = 0;
    if (!ResolveDeviceSelector(devices, selector, selected, selected_index, error)) {
      Fail("expected serial selector to resolve");
    }
    if (selected_index != 2U || selected.serial != "SN-2000") {
      Fail("unexpected serial selector resolution result");
    }
  }

  {
    DeviceSelector selector;
    std::string error;
    if (!ParseDeviceSelector("user_id:Primary,index:1", selector, error)) {
      Fail("expected user_id+index selector to parse");
    }
    DeviceInfo selected;
    std::size_t selected_index = 0;
    if (!ResolveDeviceSelector(devices, selector, selected, selected_index, error)) {
      Fail("expected user_id+index selector to resolve");
    }
    if (selected_index != 1U || !selected.ip_address.has_value() ||
        selected.ip_address.value() != "10.0.0.22") {
      Fail("unexpected user_id+index selector resolution result");
    }
  }

  {
    DeviceSelector selector;
    std::string error;
    if (!ParseDeviceSelector("serial:SN-1001", selector, error)) {
      Fail("expected ambiguous serial selector to parse");
    }
    DeviceInfo selected;
    std::size_t selected_index = 0;
    if (ResolveDeviceSelector(devices, selector, selected, selected_index, error)) {
      Fail("expected ambiguous serial selector to fail without index");
    }
    AssertContains(error, "matched multiple devices");
  }

  {
    DeviceSelector selector;
    std::string error;
    if (ParseDeviceSelector("serial:", selector, error)) {
      Fail("expected empty serial selector value to fail parsing");
    }
    AssertContains(error, "missing a value");
  }

  {
    DeviceSelector selector;
    std::string error;
    if (ParseDeviceSelector("foo:bar", selector, error)) {
      Fail("expected unsupported selector key to fail parsing");
    }
    AssertContains(error, "not supported");
  }

  {
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const fs::path temp_root =
        fs::temp_directory_path() / ("labops-real-selector-resolution-" + std::to_string(now_ms));
    const fs::path fixture_path = temp_root / "devices.csv";

    std::error_code ec;
    fs::remove_all(temp_root, ec);
    fs::create_directories(temp_root, ec);
    if (ec) {
      Fail("failed to create temp fixture root");
    }
    WriteFixtureCsv(fixture_path);
    const std::string fixture_path_text = fixture_path.string();
    ScopedEnvOverride fixture_override("LABOPS_REAL_DEVICE_FIXTURE", fixture_path_text.c_str());

    DeviceInfo selected;
    std::size_t selected_index = 0;
    std::string error;
    const bool ok =
        ResolveConnectedDevice("serial:SN-1001,index:1", selected, selected_index, error);
    if (IsRealBackendEnabledAtBuild()) {
      if (!ok) {
        Fail("expected ResolveConnectedDevice to succeed when real backend is enabled");
      }
      if (selected_index != 1U || selected.serial != "SN-1001" ||
          !selected.mac_address.has_value() ||
          selected.mac_address.value() != "AA:BB:CC:DD:EE:02" ||
          !selected.firmware_version.has_value() || selected.firmware_version.value() != "1.0.1" ||
          !selected.sdk_version.has_value() || selected.sdk_version.value() != "21.1.8") {
        Fail("unexpected ResolveConnectedDevice result");
      }
    } else {
      if (ok) {
        Fail("expected ResolveConnectedDevice to fail when real backend is unavailable");
      }
      AssertContains(error, "real backend");
    }

    fs::remove_all(temp_root, ec);
  }

  return 0;
}
