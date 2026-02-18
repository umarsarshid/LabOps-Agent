#include "backends/sdk_stub/real_camera_backend_stub.hpp"

#include <fstream>
#include <string_view>

#ifndef LABOPS_ENABLE_REAL_BACKEND
#define LABOPS_ENABLE_REAL_BACKEND 0
#endif

#ifndef LABOPS_REAL_BACKEND_REQUESTED
#define LABOPS_REAL_BACKEND_REQUESTED 0
#endif

namespace labops::backends::sdk_stub {

namespace {

std::string BuildConnectionError() {
#if LABOPS_ENABLE_REAL_BACKEND
  return "real backend path is enabled, but no proprietary SDK adapter is linked in this "
         "repository";
#else
  return "real backend path is disabled at build time (set "
         "-DLABOPS_ENABLE_REAL_BACKEND=ON to enable the stub path)";
#endif
}

std::string BuildNotConnectedError(std::string_view operation) {
  return std::string("real backend stub cannot ") + std::string(operation) +
         " before a successful connect";
}

} // namespace

bool IsRealBackendEnabledAtBuild() {
#if LABOPS_ENABLE_REAL_BACKEND
  return true;
#else
  return false;
#endif
}

bool WasRealBackendRequestedAtBuild() {
#if LABOPS_REAL_BACKEND_REQUESTED
  return true;
#else
  return false;
#endif
}

std::string_view RealBackendAvailabilityStatusText() {
#if LABOPS_ENABLE_REAL_BACKEND
  return "enabled";
#elif LABOPS_REAL_BACKEND_REQUESTED
  return "disabled (SDK not found)";
#else
  return "disabled (build option OFF)";
#endif
}

RealCameraBackendStub::RealCameraBackendStub() {
  params_ = {
      {"backend", "real_stub"},
      {"sdk_adapter", "not_integrated"},
      {"build_real_backend_enabled", IsRealBackendEnabledAtBuild() ? "true" : "false"},
  };
}

void RealCameraBackendStub::AppendSdkLog(std::string_view message) const {
  if (sdk_log_path_.empty()) {
    return;
  }
  std::ofstream out(sdk_log_path_, std::ios::app | std::ios::binary);
  if (!out) {
    return;
  }
  out << message << '\n';
}

bool RealCameraBackendStub::Connect(std::string& error) {
  if (connected_) {
    error = "real backend stub is already connected";
    AppendSdkLog("connect status=error reason=already_connected");
    return false;
  }

  error = BuildConnectionError();
  AppendSdkLog(std::string("connect status=error reason=") + error);
  return false;
}

bool RealCameraBackendStub::Start(std::string& error) {
  if (!connected_) {
    error = BuildNotConnectedError("start");
    AppendSdkLog("start status=error reason=not_connected");
    return false;
  }
  if (running_) {
    error = "real backend stub is already running";
    AppendSdkLog("start status=error reason=already_running");
    return false;
  }

  error = "real backend stub cannot start stream because SDK adapter is not implemented";
  AppendSdkLog("start status=error reason=sdk_not_implemented");
  return false;
}

bool RealCameraBackendStub::Stop(std::string& error) {
  if (!running_) {
    error = "real backend stub is not running";
    AppendSdkLog("stop status=error reason=not_running");
    return false;
  }

  error = "real backend stub cannot stop stream because no active SDK session exists";
  AppendSdkLog("stop status=error reason=sdk_not_implemented");
  return false;
}

bool RealCameraBackendStub::SetParam(const std::string& key, const std::string& value,
                                     std::string& error) {
  if (key.empty()) {
    error = "parameter key cannot be empty";
    return false;
  }
  if (value.empty()) {
    error = "parameter value cannot be empty";
    return false;
  }

  if (key == "sdk.log.path") {
    sdk_log_path_ = std::filesystem::path(value);
    std::ofstream out(sdk_log_path_, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out) {
      error = "unable to open sdk log path: " + value;
      return false;
    }
    out << "sdk_log_capture=enabled backend=real_stub\n";
    params_[key] = value;
    error.clear();
    return true;
  }

  // Preserve requested values for diagnostics even though no SDK calls occur.
  params_[key] = value;
  AppendSdkLog(std::string("set_param key=") + key + " value=" + value + " status=accepted");
  return true;
}

BackendConfig RealCameraBackendStub::DumpConfig() const {
  BackendConfig config = params_;
  config["connected"] = connected_ ? "true" : "false";
  config["running"] = running_ ? "true" : "false";
  return config;
}

std::vector<FrameSample> RealCameraBackendStub::PullFrames(std::chrono::milliseconds duration,
                                                           std::string& error) {
  if (duration < std::chrono::milliseconds::zero()) {
    error = "pull_frames duration cannot be negative";
    AppendSdkLog("pull_frames status=error reason=negative_duration");
    return {};
  }

  if (!connected_) {
    error = BuildNotConnectedError("pull_frames");
    AppendSdkLog("pull_frames status=error reason=not_connected");
    return {};
  }

  if (!running_) {
    error = "real backend stub cannot pull frames while stream is stopped";
    AppendSdkLog("pull_frames status=error reason=stream_not_running");
    return {};
  }

  error = "real backend stub cannot produce frames because SDK adapter is not implemented";
  AppendSdkLog("pull_frames status=error reason=sdk_not_implemented");
  return {};
}

} // namespace labops::backends::sdk_stub
