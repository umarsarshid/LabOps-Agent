#include "backends/webcam/webcam_backend.hpp"

#include "backends/webcam/capabilities.hpp"
#include "backends/webcam/opencv_bootstrap.hpp"

namespace labops::backends::webcam {

namespace {

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
}

std::string WebcamBackend::BuildNotAvailableError() const {
  return "BACKEND_NOT_AVAILABLE: webcam backend on " + platform_.platform_name +
         " is not ready: " + platform_.unavailability_reason;
}

bool WebcamBackend::Connect(std::string& error) {
  if (connected_) {
    error = "webcam backend is already connected";
    return false;
  }

  if (!platform_.available) {
    error = BuildNotAvailableError();
    return false;
  }

  connected_ = true;
  return true;
}

bool WebcamBackend::Start(std::string& error) {
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

  error = "BACKEND_NOT_AVAILABLE: webcam streaming loop not implemented yet";
  return false;
}

bool WebcamBackend::Stop(std::string& error) {
  if (!running_) {
    error = "webcam backend is not running";
    return false;
  }

  running_ = false;
  return true;
}

bool WebcamBackend::SetParam(const std::string& key, const std::string& value, std::string& error) {
  if (key.empty()) {
    error = "parameter key cannot be empty";
    return false;
  }
  if (value.empty()) {
    error = "parameter value cannot be empty";
    return false;
  }

  params_[key] = value;
  return true;
}

BackendConfig WebcamBackend::DumpConfig() const {
  BackendConfig config = params_;
  config["connected"] = connected_ ? "true" : "false";
  config["running"] = running_ ? "true" : "false";
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

  error = "BACKEND_NOT_AVAILABLE: webcam frame capture is not implemented yet";
  return {};
}

} // namespace labops::backends::webcam
