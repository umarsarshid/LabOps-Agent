#include "backends/real_sdk/real_backend.hpp"
#include "backends/real_sdk/acquisition_loop.hpp"
#include "backends/real_sdk/frame_provider.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace labops::backends::real_sdk {

namespace {

constexpr double kDefaultFrameRateFps = 30.0;
constexpr std::uint32_t kDefaultFrameSizeBytes = 4'096;
constexpr double kDefaultTimeoutPercent = 1.0;
constexpr double kDefaultIncompletePercent = 1.0;
constexpr std::uint64_t kDefaultSeed = 1U;

std::string BuildNotConnectedError(std::string_view operation) {
  return std::string("real backend skeleton cannot ") + std::string(operation) +
         " before a successful connect";
}

bool ParseUInt32(std::string_view raw, std::uint32_t& parsed) {
  if (raw.empty()) {
    return false;
  }
  const char* begin = raw.data();
  const char* end = begin + raw.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  return ec == std::errc() && ptr == end;
}

bool ParseUInt64(std::string_view raw, std::uint64_t& parsed) {
  if (raw.empty()) {
    return false;
  }
  const char* begin = raw.data();
  const char* end = begin + raw.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  return ec == std::errc() && ptr == end;
}

std::optional<std::uint64_t> ReadOptionalUint64Env(const char* name) {
  if (name == nullptr || *name == '\0') {
    return std::nullopt;
  }
  const char* raw = std::getenv(name);
  if (raw == nullptr || *raw == '\0') {
    return std::nullopt;
  }

  std::uint64_t parsed = 0;
  if (!ParseUInt64(raw, parsed) || parsed == 0U) {
    return std::nullopt;
  }
  return parsed;
}

bool ParseFiniteDouble(std::string_view raw, double& parsed) {
  if (raw.empty()) {
    return false;
  }
  std::string value(raw);
  char* parse_end = nullptr;
  parsed = std::strtod(value.c_str(), &parse_end);
  return parse_end != nullptr && *parse_end == '\0' && std::isfinite(parsed);
}

bool TryGetParamValue(const BackendConfig& params, std::initializer_list<const char*> keys,
                      std::string_view& value) {
  for (const char* key : keys) {
    if (key == nullptr || *key == '\0') {
      continue;
    }
    const auto it = params.find(key);
    if (it != params.end()) {
      value = it->second;
      return true;
    }
  }
  return false;
}

bool ResolveFrameRateFps(const BackendConfig& params, double& fps, std::string& error) {
  std::string_view raw_value;
  if (!TryGetParamValue(params, {"AcquisitionFrameRate", "frame_rate", "fps"}, raw_value)) {
    fps = kDefaultFrameRateFps;
    return true;
  }

  double parsed = 0.0;
  if (!ParseFiniteDouble(raw_value, parsed) || parsed <= 0.0) {
    error = "invalid AcquisitionFrameRate parameter value: " + std::string(raw_value);
    return false;
  }

  fps = parsed;
  return true;
}

bool ResolveFrameSizeBytes(const BackendConfig& params, std::uint32_t& frame_size_bytes,
                           std::string& error) {
  std::string_view raw_value;
  if (!TryGetParamValue(params, {"PayloadSize", "frame_size_bytes"}, raw_value)) {
    frame_size_bytes = kDefaultFrameSizeBytes;
    return true;
  }

  std::uint32_t parsed = 0;
  if (!ParseUInt32(raw_value, parsed) || parsed == 0U) {
    error = "invalid PayloadSize parameter value: " + std::string(raw_value);
    return false;
  }

  frame_size_bytes = parsed;
  return true;
}

bool ResolveSeed(const BackendConfig& params, std::uint64_t& seed, std::string& error) {
  std::string_view raw_value;
  if (!TryGetParamValue(params, {"FrameSeed", "seed"}, raw_value)) {
    seed = kDefaultSeed;
    return true;
  }

  std::uint64_t parsed = 0;
  if (!ParseUInt64(raw_value, parsed)) {
    error = "invalid FrameSeed parameter value: " + std::string(raw_value);
    return false;
  }

  seed = parsed;
  return true;
}

bool ResolvePercent(const BackendConfig& params, std::initializer_list<const char*> keys,
                    std::string_view canonical_key, double default_value, double& resolved,
                    std::string& error) {
  std::string_view raw_value;
  if (!TryGetParamValue(params, keys, raw_value)) {
    resolved = default_value;
    return true;
  }

  double parsed = 0.0;
  if (!ParseFiniteDouble(raw_value, parsed) || parsed < 0.0 || parsed > 100.0) {
    error = "invalid " + std::string(canonical_key) +
            " parameter value: " + std::string(raw_value) + " (expected 0..100)";
    return false;
  }

  resolved = parsed;
  return true;
}

} // namespace

RealBackend::RealBackend() {
  params_ = {
      {"backend", "real"},
      {"integration_stage", "skeleton"},
      {"sdk_adapter", "pending_vendor_integration"},
      {"stream_session", "raii"},
      {"AcquisitionFrameRate", "30"},
      {"PayloadSize", "4096"},
      {"FrameTimeoutPercent", "1.0"},
      {"FrameIncompletePercent", "1.0"},
      {"FrameSeed", "1"},
  };

  disconnect_after_pull_calls_ = ReadOptionalUint64Env("LABOPS_REAL_DISCONNECT_AFTER_PULLS");
  if (disconnect_after_pull_calls_.has_value()) {
    params_["simulate_disconnect_after_pull_calls"] =
        std::to_string(disconnect_after_pull_calls_.value());
  }
}

void RealBackend::AppendSdkLog(std::string_view message) const {
  if (sdk_log_path_.empty()) {
    return;
  }
  std::ofstream out(sdk_log_path_, std::ios::app | std::ios::binary);
  if (!out) {
    return;
  }
  out << message << '\n';
}

bool RealBackend::Connect(std::string& error) {
  if (connected_) {
    error = "real backend skeleton is already connected";
    AppendSdkLog("connect status=error reason=already_connected");
    return false;
  }
  if (simulated_disconnect_latched_) {
    // Once the fixture disconnect trips, keep connect failing so run-level
    // reconnect policy can exercise retry exhaustion deterministically.
    error = "device unavailable after disconnect";
    AppendSdkLog("connect status=error reason=device_unavailable_after_disconnect");
    return false;
  }

  // Acquire process-level SDK context first so init/shutdown behavior is
  // centralized and balanced even before camera session APIs are wired.
  if (!sdk_context_.Acquire(error)) {
    AppendSdkLog("connect status=error reason=sdk_context_acquire_failed");
    return false;
  }

  connected_ = true;
  error.clear();
  AppendSdkLog("connect status=success");
  return true;
}

bool RealBackend::Start(std::string& error) {
  if (!connected_) {
    error = BuildNotConnectedError("start");
    AppendSdkLog("start status=error reason=not_connected");
    return false;
  }
  if (!stream_session_.Start(error)) {
    AppendSdkLog("start status=error reason=stream_session_start_failed");
    return false;
  }

  if (next_frame_id_ == 0U) {
    stream_start_ts_ = std::chrono::system_clock::now();
  }
  error.clear();
  AppendSdkLog("start status=success");
  return true;
}

bool RealBackend::Stop(std::string& error) {
  if (!connected_ && !stream_session_.running()) {
    error.clear();
    AppendSdkLog("stop status=success reason=already_stopped");
    return true;
  }
  if (!connected_) {
    error = BuildNotConnectedError("stop");
    AppendSdkLog("stop status=error reason=not_connected");
    return false;
  }
  if (!stream_session_.Stop(error)) {
    AppendSdkLog("stop status=error reason=stream_session_stop_failed");
    return false;
  }
  AppendSdkLog("stop status=success");
  return true;
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

  if (key == "sdk.log.path") {
    sdk_log_path_ = std::filesystem::path(value);
    std::ofstream out(sdk_log_path_, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out) {
      error = "unable to open sdk log path: " + value;
      return false;
    }
    out << "sdk_log_capture=enabled backend=real\n";
    params_[key] = value;
    error.clear();
    return true;
  }

  // Preserve requested values for diagnostics before SDK mapping is wired.
  params_[key] = value;
  AppendSdkLog(std::string("set_param key=") + key + " value=" + value + " status=accepted");
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
    AppendSdkLog("pull_frames status=error reason=negative_duration");
    return {};
  }

  if (!connected_) {
    error = BuildNotConnectedError("pull_frames");
    AppendSdkLog("pull_frames status=error reason=not_connected");
    return {};
  }

  if (!stream_session_.running()) {
    error = "real backend skeleton cannot pull frames while stream is stopped";
    AppendSdkLog("pull_frames status=error reason=stream_not_running");
    return {};
  }

  if (duration == std::chrono::milliseconds::zero()) {
    error.clear();
    AppendSdkLog("pull_frames status=success frames=0 reason=zero_duration");
    return {};
  }

  ++pull_calls_;
  if (disconnect_after_pull_calls_.has_value() &&
      pull_calls_ >= disconnect_after_pull_calls_.value()) {
    // Simulate a mid-stream device detach in OSS builds so reconnect policy can
    // be tested without physical unplug events.
    std::string stop_error;
    (void)stream_session_.Stop(stop_error);
    simulated_disconnect_latched_ = true;
    connected_ = false;
    error = "device disconnected during acquisition";
    AppendSdkLog("pull_frames status=error reason=device_disconnected");
    return {};
  }

  double frame_rate_fps = kDefaultFrameRateFps;
  if (!ResolveFrameRateFps(params_, frame_rate_fps, error)) {
    return {};
  }

  std::uint32_t frame_size_bytes = kDefaultFrameSizeBytes;
  if (!ResolveFrameSizeBytes(params_, frame_size_bytes, error)) {
    return {};
  }

  std::uint64_t seed = kDefaultSeed;
  if (!ResolveSeed(params_, seed, error)) {
    return {};
  }

  double timeout_percent = kDefaultTimeoutPercent;
  if (!ResolvePercent(params_, {"FrameTimeoutPercent", "frame_timeout_percent", "timeout_percent"},
                      "FrameTimeoutPercent", kDefaultTimeoutPercent, timeout_percent, error)) {
    return {};
  }

  double incomplete_percent = kDefaultIncompletePercent;
  if (!ResolvePercent(
          params_, {"FrameIncompletePercent", "frame_incomplete_percent", "incomplete_percent"},
          "FrameIncompletePercent", kDefaultIncompletePercent, incomplete_percent, error)) {
    return {};
  }

  // Timeout and incomplete percentages share one probability bucket.
  incomplete_percent = std::min(incomplete_percent, 100.0 - timeout_percent);

  DeterministicFrameProvider provider(seed, frame_size_bytes, timeout_percent, incomplete_percent);
  AcquisitionLoopInput loop_input;
  loop_input.duration = duration;
  loop_input.frame_rate_fps = frame_rate_fps;
  loop_input.default_frame_size_bytes = frame_size_bytes;
  loop_input.first_frame_id = next_frame_id_;
  loop_input.stream_start_ts = stream_start_ts_;

  AcquisitionLoopResult loop_result;
  if (!RunAcquisitionLoop(provider, loop_input, loop_result, error)) {
    AppendSdkLog("pull_frames status=error reason=acquisition_loop_failed");
    return {};
  }

  next_frame_id_ = loop_result.next_frame_id;
  error.clear();
  AppendSdkLog(std::string("pull_frames status=success frames=") +
               std::to_string(loop_result.frames.size()) +
               " timeout=" + std::to_string(loop_result.counters.frames_timeout) +
               " incomplete=" + std::to_string(loop_result.counters.frames_incomplete) +
               " stall_periods=" + std::to_string(loop_result.counters.stall_periods_total));
  return loop_result.frames;
}

} // namespace labops::backends::real_sdk
