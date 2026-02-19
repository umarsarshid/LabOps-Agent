#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace labops::backends::webcam {

// Typed control IDs shared across webcam discovery and apply logic.
//
// Why this exists:
// - keeps platform-specific control names behind one normalized enum
// - makes capability reporting and scenario mapping deterministic
// - allows partial support to be represented by control presence/absence
//   in `SupportedControls`
enum class WebcamControlId {
  kWidth = 0,
  kHeight,
  kFps,
  kPixelFormat,
  kExposure,
  kGain,
  kAutoExposure,
  kAutoFpsHint,
};

// Value-shape classification for one control.
//
// Example:
// - `width` => integer
// - `gain` => float
// - `auto_exposure` => boolean
// - `pixel_format` => enum
//
// The type plus optional range/enum metadata is enough to emit human- and
// machine-friendly capability evidence before any frame loop runs.
enum class WebcamControlValueType {
  kInteger = 0,
  kFloat,
  kBoolean,
  kEnum,
};

// Numeric range metadata for integer/float controls.
//
// Fields remain optional so platforms can report partial information safely.
struct WebcamControlRange {
  std::optional<double> min;
  std::optional<double> max;
  std::optional<double> step;
};

// Full capability spec for one control ID.
struct WebcamControlSpec {
  WebcamControlValueType value_type = WebcamControlValueType::kInteger;
  WebcamControlRange range;
  std::vector<std::string> enum_values;
  bool read_only = false;
};

// Normalized control-capability table for one webcam device.
//
// Semantics:
// - present key => supported control
// - missing key => unsupported control
using SupportedControls = std::map<WebcamControlId, WebcamControlSpec>;

// Minimal normalized webcam identity and control capability snapshot.
struct WebcamDeviceInfo {
  std::string device_id;
  std::string friendly_name;
  std::optional<std::string> bus_info;
  SupportedControls supported_controls;
};

const char* ToString(WebcamControlId control_id);
const char* ToString(WebcamControlValueType value_type);

// Convenience helper for feature checks in backend and tests.
bool SupportsControl(const SupportedControls& controls, WebcamControlId control_id);

// JSON-friendly serializers for capability evidence artifacts.
std::string ToJson(const WebcamControlSpec& spec);
std::string ToJson(const SupportedControls& controls);
std::string ToJson(const WebcamDeviceInfo& device);

} // namespace labops::backends::webcam
