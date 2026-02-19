#include "backends/webcam/device_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(const std::string& text, std::string_view needle) {
  if (text.find(needle) == std::string::npos) {
    std::cerr << "expected output to contain: " << needle << '\n';
    std::cerr << "actual output:\n" << text << '\n';
    std::abort();
  }
}

void AssertNotContains(const std::string& text, std::string_view needle) {
  if (text.find(needle) != std::string::npos) {
    std::cerr << "expected output to omit: " << needle << '\n';
    std::cerr << "actual output:\n" << text << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  using labops::backends::webcam::SupportedControls;
  using labops::backends::webcam::SupportsControl;
  using labops::backends::webcam::ToJson;
  using labops::backends::webcam::WebcamControlId;
  using labops::backends::webcam::WebcamControlSpec;
  using labops::backends::webcam::WebcamControlValueType;
  using labops::backends::webcam::WebcamDeviceInfo;

  WebcamDeviceInfo device;
  device.device_id = "webcam-0";
  device.friendly_name = "USB UVC Camera";
  device.bus_info = "usb-0000:00:14.0-2";

  SupportedControls controls;
  controls[WebcamControlId::kWidth] = WebcamControlSpec{
      .value_type = WebcamControlValueType::kInteger,
      .range = {.min = 640.0, .max = 1920.0, .step = 1.0},
      .enum_values = {},
      .read_only = false,
  };
  controls[WebcamControlId::kHeight] = WebcamControlSpec{
      .value_type = WebcamControlValueType::kInteger,
      .range = {.min = 480.0, .max = 1080.0, .step = 1.0},
      .enum_values = {},
      .read_only = false,
  };
  controls[WebcamControlId::kFps] = WebcamControlSpec{
      .value_type = WebcamControlValueType::kInteger,
      .range = {.min = 5.0, .max = 60.0, .step = 1.0},
      .enum_values = {},
      .read_only = false,
  };
  controls[WebcamControlId::kPixelFormat] = WebcamControlSpec{
      .value_type = WebcamControlValueType::kEnum,
      .range = {},
      .enum_values = {"MJPG", "YUYV"},
      .read_only = false,
  };

  device.supported_controls = std::move(controls);

  if (!SupportsControl(device.supported_controls, WebcamControlId::kWidth)) {
    Fail("width should be marked as supported");
  }
  if (!SupportsControl(device.supported_controls, WebcamControlId::kHeight)) {
    Fail("height should be marked as supported");
  }
  if (!SupportsControl(device.supported_controls, WebcamControlId::kFps)) {
    Fail("fps should be marked as supported");
  }
  if (!SupportsControl(device.supported_controls, WebcamControlId::kPixelFormat)) {
    Fail("pixel_format should be marked as supported");
  }

  // This is the key contract: omitted controls are represented as unsupported.
  if (SupportsControl(device.supported_controls, WebcamControlId::kExposure)) {
    Fail("exposure should be marked as unsupported by omission");
  }

  const std::string json = ToJson(device);
  AssertContains(json, "\"device_id\":\"webcam-0\"");
  AssertContains(json, "\"friendly_name\":\"USB UVC Camera\"");
  AssertContains(json, "\"supported_controls\"");
  AssertContains(json, "\"width\"");
  AssertContains(json, "\"height\"");
  AssertContains(json, "\"fps\"");
  AssertContains(json, "\"pixel_format\"");
  AssertContains(json, "\"enum_values\":[\"MJPG\",\"YUYV\"]");
  AssertNotContains(json, "\"exposure\"");

  std::cout << "webcam_device_model_smoke: ok\n";
  return 0;
}
