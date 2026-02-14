#include "backends/sdk_stub/real_camera_backend_stub.hpp"

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

bool RealCameraBackendStub::Connect(std::string& error) {
  if (connected_) {
    error = "real backend stub is already connected";
    return false;
  }

  error = BuildConnectionError();
  return false;
}

bool RealCameraBackendStub::Start(std::string& error) {
  if (!connected_) {
    error = BuildNotConnectedError("start");
    return false;
  }
  if (running_) {
    error = "real backend stub is already running";
    return false;
  }

  error = "real backend stub cannot start stream because SDK adapter is not implemented";
  return false;
}

bool RealCameraBackendStub::Stop(std::string& error) {
  if (!running_) {
    error = "real backend stub is not running";
    return false;
  }

  error = "real backend stub cannot stop stream because no active SDK session exists";
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

  // Preserve requested values for diagnostics even though no SDK calls occur.
  params_[key] = value;
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
    return {};
  }

  if (!connected_) {
    error = BuildNotConnectedError("pull_frames");
    return {};
  }

  if (!running_) {
    error = "real backend stub cannot pull frames while stream is stopped";
    return {};
  }

  error = "real backend stub cannot produce frames because SDK adapter is not implemented";
  return {};
}

} // namespace labops::backends::sdk_stub
