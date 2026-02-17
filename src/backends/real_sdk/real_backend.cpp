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
      {"stream_session", "raii"},
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

  connected_ = true;
  error.clear();
  return true;
}

bool RealBackend::Start(std::string& error) {
  if (!connected_) {
    error = BuildNotConnectedError("start");
    return false;
  }
  return stream_session_.Start(error);
}

bool RealBackend::Stop(std::string& error) {
  if (!connected_) {
    error = BuildNotConnectedError("stop");
    return false;
  }
  return stream_session_.Stop(error);
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
  config["running"] = stream_session_.running() ? "true" : "false";
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

  if (!stream_session_.running()) {
    error = "real backend skeleton cannot pull frames while stream is stopped";
    return {};
  }

  error =
      "real backend skeleton started acquisition, but frame retrieval adapter is not implemented";
  return {};
}

} // namespace labops::backends::real_sdk
