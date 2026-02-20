#include "backends/webcam/device_model.hpp"

#include "core/json_utils.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace labops::backends::webcam {

namespace {

std::string FormatJsonNumber(const double value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  std::ostringstream out;
  out << std::setprecision(15) << value;
  return out.str();
}

void WriteJsonDoubleField(std::ostream& out, std::string_view key, const double value,
                          bool& first_field) {
  if (!first_field) {
    out << ',';
  }
  first_field = false;
  out << '"' << key << "\":" << FormatJsonNumber(value);
}

} // namespace

const char* ToString(const WebcamControlId control_id) {
  switch (control_id) {
  case WebcamControlId::kWidth:
    return "width";
  case WebcamControlId::kHeight:
    return "height";
  case WebcamControlId::kFps:
    return "fps";
  case WebcamControlId::kPixelFormat:
    return "pixel_format";
  case WebcamControlId::kExposure:
    return "exposure";
  case WebcamControlId::kGain:
    return "gain";
  case WebcamControlId::kAutoExposure:
    return "auto_exposure";
  case WebcamControlId::kAutoFpsHint:
    return "auto_fps_hint";
  }
  return "unknown";
}

const char* ToString(const WebcamControlValueType value_type) {
  switch (value_type) {
  case WebcamControlValueType::kInteger:
    return "integer";
  case WebcamControlValueType::kFloat:
    return "float";
  case WebcamControlValueType::kBoolean:
    return "boolean";
  case WebcamControlValueType::kEnum:
    return "enum";
  }
  return "integer";
}

bool SupportsControl(const SupportedControls& controls, const WebcamControlId control_id) {
  return controls.find(control_id) != controls.end();
}

std::string ToJson(const WebcamControlSpec& spec) {
  std::ostringstream out;
  out << "{\"value_type\":\"" << ToString(spec.value_type) << "\",\"range\":{";

  bool first_range_field = true;
  if (spec.range.min.has_value()) {
    WriteJsonDoubleField(out, "min", spec.range.min.value(), first_range_field);
  }
  if (spec.range.max.has_value()) {
    WriteJsonDoubleField(out, "max", spec.range.max.value(), first_range_field);
  }
  if (spec.range.step.has_value()) {
    WriteJsonDoubleField(out, "step", spec.range.step.value(), first_range_field);
  }

  out << "},\"enum_values\":[";
  for (std::size_t i = 0; i < spec.enum_values.size(); ++i) {
    if (i > 0U) {
      out << ',';
    }
    out << '"' << core::EscapeJson(spec.enum_values[i]) << '"';
  }

  out << "],\"read_only\":" << (spec.read_only ? "true" : "false") << '}';
  return out.str();
}

std::string ToJson(const SupportedControls& controls) {
  std::ostringstream out;
  out << '{';
  bool first_control = true;
  for (const auto& [control_id, spec] : controls) {
    if (!first_control) {
      out << ',';
    }
    first_control = false;

    out << '"' << core::EscapeJson(ToString(control_id)) << "\":" << ToJson(spec);
  }
  out << '}';
  return out.str();
}

std::string ToJson(const WebcamDeviceInfo& device) {
  std::ostringstream out;
  out << "{\"device_id\":\"" << core::EscapeJson(device.device_id) << "\",";
  out << "\"friendly_name\":\"" << core::EscapeJson(device.friendly_name) << "\",";
  out << "\"bus_info\":";
  if (device.bus_info.has_value()) {
    out << '"' << core::EscapeJson(device.bus_info.value()) << '"';
  } else {
    out << "null";
  }
  out << ",\"capture_index\":";
  if (device.capture_index.has_value()) {
    out << device.capture_index.value();
  } else {
    out << "null";
  }
  out << ",\"supported_controls\":" << ToJson(device.supported_controls) << '}';
  return out.str();
}

} // namespace labops::backends::webcam
