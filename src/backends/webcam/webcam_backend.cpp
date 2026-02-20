#include "backends/webcam/webcam_backend.hpp"

#include "backends/webcam/capabilities.hpp"
#include "backends/webcam/opencv_bootstrap.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace labops::backends::webcam {

namespace {

bool ParseNonNegativeSize(std::string_view text, std::size_t& value) {
  if (text.empty()) {
    return false;
  }
  std::size_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return false;
  }
  value = parsed;
  return true;
}

bool ParsePositiveUInt32(std::string_view text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }
  std::uint32_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end || parsed == 0U) {
    return false;
  }
  value = parsed;
  return true;
}

bool ParsePositiveDouble(std::string_view text, double& value) {
  if (text.empty()) {
    return false;
  }
  std::string copy(text);
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(copy, &consumed);
    if (consumed != copy.size()) {
      return false;
    }
    if (!std::isfinite(parsed) || parsed <= 0.0) {
      return false;
    }
    value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

std::string FormatDouble(const double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::string ToUpperAscii(std::string value) {
  for (char& c : value) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - ('a' - 'A'));
    }
  }
  return value;
}

void RemoveKeysWithPrefix(const std::string_view prefix, BackendConfig& config) {
  for (auto it = config.begin(); it != config.end();) {
    if (it->first.rfind(prefix.data(), 0U) == 0U) {
      it = config.erase(it);
      continue;
    }
    ++it;
  }
}

void AddCapabilityToConfig(const char* key, const CapabilityState capability,
                           BackendConfig& config) {
  config[key] = ToString(capability);
}

} // namespace

WebcamBackend::WebcamBackend() : platform_(ProbePlatformAvailability()) {
  params_["backend"] = platform_.backend_name;
  params_["platform"] = platform_.platform_name;
  params_["platform_available"] = platform_.available ? "true" : "false";
  params_["platform_reason"] = platform_.unavailability_reason;
  params_["opencv_bootstrap_enabled"] = IsOpenCvBootstrapEnabled() ? "true" : "false";
  params_["opencv_bootstrap_status"] = OpenCvBootstrapStatusText();
  params_["opencv_bootstrap_detail"] = OpenCvBootstrapDetail();

  AddCapabilityToConfig("capability.exposure", platform_.capabilities.exposure, params_);
  AddCapabilityToConfig("capability.gain", platform_.capabilities.gain, params_);
  AddCapabilityToConfig("capability.pixel_format", platform_.capabilities.pixel_format, params_);
  AddCapabilityToConfig("capability.roi", platform_.capabilities.roi, params_);
  AddCapabilityToConfig("capability.trigger", platform_.capabilities.trigger, params_);
  AddCapabilityToConfig("capability.frame_rate", platform_.capabilities.frame_rate, params_);

  // Keep index explicit so selectorless runs still target index 0
  // deterministically.
  params_["device.index"] = "0";
}

std::string WebcamBackend::BuildNotAvailableError() const {
  return "BACKEND_NOT_AVAILABLE: webcam backend on " + platform_.platform_name +
         " is not ready: " + platform_.unavailability_reason;
}

void WebcamBackend::ClearSessionConfigSnapshot() {
  unsupported_controls_.clear();
  RemoveKeysWithPrefix("webcam.actual_", params_);
  RemoveKeysWithPrefix("webcam.unsupported.", params_);
  RemoveKeysWithPrefix("webcam.linux_capture.", params_);
}

void WebcamBackend::RecordUnsupportedControl(std::string key, std::string requested_value,
                                             std::string reason) {
  unsupported_controls_.push_back({.key = std::move(key),
                                   .requested_value = std::move(requested_value),
                                   .reason = std::move(reason)});
}

bool WebcamBackend::ResolveDeviceIndex(std::size_t& index, std::string& error) const {
  error.clear();
  index = 0;
  const auto it = params_.find("device.index");
  if (it == params_.end()) {
    return true;
  }

  if (!ParseNonNegativeSize(it->second, index)) {
    error = "device.index must be a non-negative integer";
    return false;
  }
  return true;
}

bool WebcamBackend::ApplyRequestedConfig(std::string& error) {
  error.clear();
  ClearSessionConfigSnapshot();

  auto apply_numeric_property = [&](std::string_view key, const std::optional<double> requested,
                                    const OpenCvCaptureProperty property) {
    if (!requested.has_value()) {
      return;
    }

    const std::string requested_text = FormatDouble(requested.value());
    params_[std::string(key)] = requested_text;

    std::string set_error;
    const bool set_ok = opencv_.SetProperty(property, requested.value(), set_error);

    double actual_value = 0.0;
    std::string read_error;
    const bool read_ok = opencv_.GetProperty(property, actual_value, read_error);
    if (read_ok) {
      const std::string actual_key = "webcam.actual_" + std::string(ToString(property));
      params_[actual_key] = FormatDouble(actual_value);
    }

    if (set_ok && read_ok) {
      return;
    }

    std::string reason;
    if (!set_ok && !read_ok) {
      reason = set_error + "; " + read_error;
    } else if (!set_ok) {
      reason = set_error;
    } else {
      reason = "OpenCV cannot confirm applied value: " + read_error;
    }
    RecordUnsupportedControl(std::string(key), requested_text, std::move(reason));
  };

  apply_numeric_property("webcam.requested_width",
                         requested_.width.has_value()
                             ? std::optional<double>(static_cast<double>(requested_.width.value()))
                             : std::nullopt,
                         OpenCvCaptureProperty::kFrameWidth);
  apply_numeric_property("webcam.requested_height",
                         requested_.height.has_value()
                             ? std::optional<double>(static_cast<double>(requested_.height.value()))
                             : std::nullopt,
                         OpenCvCaptureProperty::kFrameHeight);
  apply_numeric_property("webcam.requested_fps", requested_.fps, OpenCvCaptureProperty::kFps);

  if (requested_.pixel_format.has_value()) {
    const std::string requested_text = requested_.pixel_format.value();
    params_["webcam.requested_pixel_format"] = requested_text;

    std::string set_error;
    const bool set_ok = opencv_.SetFourcc(requested_text, set_error);

    std::string actual_fourcc;
    std::string read_error;
    const bool read_ok = opencv_.GetFourcc(actual_fourcc, read_error);
    if (read_ok) {
      params_["webcam.actual_pixel_format"] = actual_fourcc;
    }

    if (set_ok && read_ok) {
      return true;
    }

    std::string reason;
    if (!set_ok && !read_ok) {
      reason = set_error + "; " + read_error;
    } else if (!set_ok) {
      reason = set_error;
    } else {
      reason = "OpenCV cannot confirm applied value: " + read_error;
    }
    RecordUnsupportedControl("webcam.requested_pixel_format", requested_text, std::move(reason));
  }

  return true;
}

bool WebcamBackend::Connect(std::string& error) {
  error.clear();
  if (connected_) {
    error = "webcam backend is already connected";
    return false;
  }

  if (!platform_.available) {
    error = BuildNotAvailableError();
    return false;
  }

  std::size_t device_index = 0;
  if (!ResolveDeviceIndex(device_index, error)) {
    return false;
  }

#if defined(__linux__)
  const std::string native_device_path = "/dev/video" + std::to_string(device_index);
  V4l2OpenInfo native_open_info;
  std::string native_probe_error;
  if (linux_capture_probe_.Open(native_device_path, native_open_info, native_probe_error)) {
    params_["webcam.linux_capture.path"] = native_open_info.device_path;
    params_["webcam.linux_capture.driver"] = native_open_info.driver_name;
    params_["webcam.linux_capture.card"] = native_open_info.card_name;
    params_["webcam.linux_capture.capabilities_hex"] = native_open_info.capabilities_hex;
    params_["webcam.linux_capture.method"] = ToString(native_open_info.capture_method);
    params_["webcam.linux_capture.method_reason"] = native_open_info.capture_method_reason;

    std::string native_close_error;
    if (!linux_capture_probe_.Close(native_close_error)) {
      error = "failed to close Linux V4L2 probe device: " + native_close_error;
      return false;
    }
  } else {
    // Keep probe errors as evidence but do not fail OpenCV bootstrap path yet.
    params_["webcam.linux_capture.path"] = native_device_path;
    params_["webcam.linux_capture.error"] = native_probe_error;
  }
#endif

  if (!opencv_.OpenDevice(device_index, error)) {
    return false;
  }

  connected_ = true;
  next_frame_id_ = 0;
  params_["device.opened_index"] = std::to_string(device_index);
  if (!ApplyRequestedConfig(error)) {
    std::string close_error;
    (void)opencv_.CloseDevice(close_error);
    connected_ = false;
    return false;
  }
  return true;
}

bool WebcamBackend::Start(std::string& error) {
  error.clear();
  if (!connected_) {
    error = "webcam backend must be connected before start";
    return false;
  }
  if (running_) {
    error = "webcam backend is already running";
    return false;
  }
  if (!platform_.available) {
    error = BuildNotAvailableError();
    return false;
  }
  if (!opencv_.IsDeviceOpen()) {
    error = "webcam backend has no open capture session";
    return false;
  }

  running_ = true;
  return true;
}

bool WebcamBackend::Stop(std::string& error) {
  error.clear();
  if (!running_) {
    error = "webcam backend is not running";
    return false;
  }

  running_ = false;
  if (!opencv_.CloseDevice(error)) {
    return false;
  }
  connected_ = false;
  return true;
}

bool WebcamBackend::SetParam(const std::string& key, const std::string& value, std::string& error) {
  error.clear();
  if (key.empty()) {
    error = "parameter key cannot be empty";
    return false;
  }
  if (value.empty()) {
    error = "parameter value cannot be empty";
    return false;
  }

  if (key == "device.index") {
    std::size_t parsed_index = 0;
    if (!ParseNonNegativeSize(value, parsed_index)) {
      error = "device.index must be a non-negative integer";
      return false;
    }
    params_[key] = std::to_string(parsed_index);
    return true;
  }

  if (key == "webcam.requested_width") {
    std::uint32_t parsed = 0;
    if (!ParsePositiveUInt32(value, parsed)) {
      error = "webcam.requested_width must be a positive integer";
      return false;
    }
    requested_.width = parsed;
    params_[key] = std::to_string(parsed);
    return true;
  }

  if (key == "webcam.requested_height") {
    std::uint32_t parsed = 0;
    if (!ParsePositiveUInt32(value, parsed)) {
      error = "webcam.requested_height must be a positive integer";
      return false;
    }
    requested_.height = parsed;
    params_[key] = std::to_string(parsed);
    return true;
  }

  if (key == "webcam.requested_fps") {
    double parsed = 0.0;
    if (!ParsePositiveDouble(value, parsed)) {
      error = "webcam.requested_fps must be a positive number";
      return false;
    }
    requested_.fps = parsed;
    params_[key] = FormatDouble(parsed);
    return true;
  }

  if (key == "webcam.requested_pixel_format") {
    if (value.size() != 4U) {
      error = "webcam.requested_pixel_format must be exactly 4 characters (example: MJPG)";
      return false;
    }
    requested_.pixel_format = ToUpperAscii(value);
    params_[key] = requested_.pixel_format.value();
    return true;
  }

  params_[key] = value;
  return true;
}

BackendConfig WebcamBackend::DumpConfig() const {
  BackendConfig config = params_;
  config["connected"] = connected_ ? "true" : "false";
  config["running"] = running_ ? "true" : "false";
  config["webcam.unsupported.count"] = std::to_string(unsupported_controls_.size());
  for (std::size_t i = 0; i < unsupported_controls_.size(); ++i) {
    const std::string prefix = "webcam.unsupported." + std::to_string(i);
    config[prefix + ".key"] = unsupported_controls_[i].key;
    config[prefix + ".requested"] = unsupported_controls_[i].requested_value;
    config[prefix + ".reason"] = unsupported_controls_[i].reason;
  }
  return config;
}

std::vector<FrameSample> WebcamBackend::PullFrames(std::chrono::milliseconds duration,
                                                   std::string& error) {
  if (duration < std::chrono::milliseconds::zero()) {
    error = "pull_frames duration cannot be negative";
    return {};
  }
  if (!connected_) {
    error = "webcam backend must be connected before pull_frames";
    return {};
  }
  if (!running_) {
    error = "webcam backend must be running before pull_frames";
    return {};
  }

  std::vector<FrameSample> frames = opencv_.PullFrames(duration, next_frame_id_, error);
  if (!error.empty()) {
    return {};
  }
  return frames;
}

} // namespace labops::backends::webcam
