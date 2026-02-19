#include "../common/assertions.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"
#include "backends/webcam/device_selector.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
  const char* name_;
  std::optional<std::string> previous_;
};

void AssertSelectorParses(std::string_view text, std::string_view expected_key) {
  labops::backends::webcam::WebcamDeviceSelector selector;
  std::string error;
  if (!labops::backends::webcam::ParseWebcamDeviceSelector(text, selector, error)) {
    Fail("expected selector parse success for: " + std::string(text) + "; error=" + error);
  }

  if (expected_key == "id" && !selector.id.has_value()) {
    Fail("expected id selector key");
  }
  if (expected_key == "index" && !selector.index.has_value()) {
    Fail("expected index selector key");
  }
  if (expected_key == "name_contains" && !selector.name_contains.has_value()) {
    Fail("expected name_contains selector key");
  }
}

} // namespace

int main() {
  using labops::backends::webcam::ResolveWebcamDeviceSelector;
  using labops::backends::webcam::ToString;
  using labops::backends::webcam::WebcamDeviceInfo;
  using labops::backends::webcam::WebcamDeviceSelector;
  using labops::backends::webcam::WebcamSelectionResult;

  AssertSelectorParses("id:cam-2", "id");
  AssertSelectorParses("index:1", "index");
  AssertSelectorParses("name_contains:brio", "name_contains");

  {
    WebcamDeviceSelector selector;
    std::string error;
    if (labops::backends::webcam::ParseWebcamDeviceSelector("unknown:value", selector, error)) {
      Fail("expected unsupported selector key to fail parsing");
    }
    AssertContains(error, "not supported");
  }

  {
    const std::vector<WebcamDeviceInfo> devices = {
        {.device_id = "cam-2", .friendly_name = "Logitech Brio"},
        {.device_id = "cam-1", .friendly_name = "FaceTime HD"},
        {.device_id = "cam-3", .friendly_name = "Logitech C920"},
    };

    {
      WebcamDeviceSelector selector{.id = std::optional<std::string>("cam-2")};
      WebcamSelectionResult selected;
      std::string error;
      if (!ResolveWebcamDeviceSelector(devices, selector, selected, error)) {
        Fail("expected id selector to resolve");
      }
      if (selected.device.device_id != "cam-2" || selected.index != 1U ||
          std::string(ToString(selected.rule)) != "id") {
        Fail("unexpected id selector resolution result");
      }
    }

    {
      WebcamDeviceSelector selector{.index = std::optional<std::size_t>(2U)};
      WebcamSelectionResult selected;
      std::string error;
      if (!ResolveWebcamDeviceSelector(devices, selector, selected, error)) {
        Fail("expected index selector to resolve");
      }
      if (selected.device.device_id != "cam-3" || selected.index != 2U ||
          std::string(ToString(selected.rule)) != "index") {
        Fail("unexpected index selector resolution result");
      }
    }

    {
      WebcamDeviceSelector selector{.name_contains = std::optional<std::string>("c920")};
      WebcamSelectionResult selected;
      std::string error;
      if (!ResolveWebcamDeviceSelector(devices, selector, selected, error)) {
        Fail("expected name_contains selector to resolve");
      }
      if (selected.device.device_id != "cam-3" || selected.index != 2U ||
          std::string(ToString(selected.rule)) != "name_contains") {
        Fail("unexpected name_contains selector resolution result");
      }
    }

    {
      WebcamDeviceSelector selector;
      WebcamSelectionResult selected;
      std::string error;
      if (!ResolveWebcamDeviceSelector(devices, selector, selected, error)) {
        Fail("expected default selector to resolve");
      }
      if (selected.device.device_id != "cam-1" || selected.index != 0U ||
          std::string(ToString(selected.rule)) != "default_index_0") {
        Fail("unexpected default selector resolution result");
      }
    }
  }

  {
    const std::filesystem::path temp_root =
        labops::tests::common::CreateUniqueTempDir("labops-webcam-selector-smoke");
    const std::filesystem::path fixture_path = temp_root / "webcams.csv";
    WriteFixtureFile(
        fixture_path,
        "# webcam fixture\n"
        "device_id,friendly_name,bus_info\n"
        "cam-20,USB Camera 20,usb:2-1\n"
        "cam-10,USB Camera 10,usb:1-3\n");

    const std::string fixture_path_text = fixture_path.string();
    ScopedEnvOverride fixture_override("LABOPS_WEBCAM_DEVICE_FIXTURE", fixture_path_text.c_str());

    std::vector<WebcamDeviceInfo> devices;
    std::string error;
    if (!labops::backends::webcam::EnumerateConnectedDevices(devices, error)) {
      Fail("expected fixture-based webcam enumeration to succeed: " + error);
    }
    if (devices.size() != 2U) {
      Fail("unexpected webcam fixture discovery count");
    }
    if (devices[0].device_id != "cam-10" || devices[1].device_id != "cam-20") {
      Fail("expected stable sorted webcam enumeration order");
    }
  }

  return 0;
}
