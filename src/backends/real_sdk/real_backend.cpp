#include "backends/real_sdk/real_backend.hpp"

#include <string_view>

namespace labops::backends::real_sdk {

namespace {

std::string BuildNotConnectedError(std::string_view operation) {
  return std::string("real backend skeleton cannot ") + std::string(operation) +
         " before a successful connect";
}

} // namespace

RealBackend::RealBackend() {
  params_ = {
      {"backend", "real"},
      {"integration_stage", "skeleton"},
      {"sdk_adapter", "pending_vendor_integration"},
  };
}

bool RealBackend::Connect(std::string& error) {
  if (connected_) {
    error = "real backend skeleton is already connected";
    return false;
  }

  // Acquire process-level SDK context first so init/shutdown behavior is
  // centralized and balanced even before camera session APIs are wired.
  if (!sdk_context_.Acquire(error)) {
    return false;
  }

  // Skeleton keeps connect non-operational until camera session wiring lands.
  // Release immediately so failed connect attempts do not pin SDK lifetime.
  sdk_context_.Release();
  error =
      "real backend skeleton initialized SDK context but camera session wiring is not implemented";
  return false;
}

bool RealBackend::Start(std::string& error) {
  if (!connected_) {
    error = BuildNotConnectedError("start");
    return false;
  }
  if (running_) {
    error = "real backend skeleton is already running";
    return false;
  }

  error = "real backend skeleton cannot start stream because SDK adapter is not implemented";
  return false;
}

bool RealBackend::Stop(std::string& error) {
  if (!running_) {
    error = "real backend skeleton is not running";
    return false;
  }

  error = "real backend skeleton cannot stop stream because no active SDK session exists";
  return false;
}

bool RealBackend::SetParam(const std::string& key, const std::string& value, std::string& error) {
  if (key.empty()) {
    error = "parameter key cannot be empty";
    return false;
  }
  if (value.empty()) {
    error = "parameter value cannot be empty";
    return false;
  }

  // Preserve requested values for diagnostics before SDK mapping is wired.
  params_[key] = value;
  return true;
}

BackendConfig RealBackend::DumpConfig() const {
  BackendConfig config = params_;
  config["connected"] = connected_ ? "true" : "false";
  config["running"] = running_ ? "true" : "false";
  return config;
}

std::vector<FrameSample> RealBackend::PullFrames(std::chrono::milliseconds duration,
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
    error = "real backend skeleton cannot pull frames while stream is stopped";
    return {};
  }

  error = "real backend skeleton cannot produce frames because SDK adapter is not implemented";
  return {};
}

} // namespace labops::backends::real_sdk
