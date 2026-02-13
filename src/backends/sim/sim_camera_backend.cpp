#include "backends/sim/sim_camera_backend.hpp"

#include <charconv>

namespace labops::backends::sim {

namespace {

constexpr std::uint32_t kDefaultFps = 30;

bool ParsePositiveUInt32(const std::string& text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint32_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end || parsed == 0) {
    return false;
  }

  value = parsed;
  return true;
}

} // namespace

SimCameraBackend::SimCameraBackend() {
  params_ = {
      {"backend", "sim"},
      {"fps", std::to_string(kDefaultFps)},
      {"pixel_format", "mono8"},
      {"trigger_mode", "free_run"},
  };
}

bool SimCameraBackend::Connect(std::string& error) {
  if (connected_) {
    error = "sim backend is already connected";
    return false;
  }

  connected_ = true;
  return true;
}

bool SimCameraBackend::Start(std::string& error) {
  if (!connected_) {
    error = "sim backend must be connected before start";
    return false;
  }

  if (running_) {
    error = "sim backend is already running";
    return false;
  }

  running_ = true;
  return true;
}

bool SimCameraBackend::Stop(std::string& error) {
  if (!running_) {
    error = "sim backend is not running";
    return false;
  }

  running_ = false;
  return true;
}

bool SimCameraBackend::SetParam(const std::string& key, const std::string& value,
                                std::string& error) {
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

BackendConfig SimCameraBackend::DumpConfig() const {
  BackendConfig config = params_;
  config["connected"] = connected_ ? "true" : "false";
  config["running"] = running_ ? "true" : "false";
  return config;
}

std::vector<FrameSample> SimCameraBackend::PullFrames(std::chrono::milliseconds duration,
                                                      std::string& error) {
  if (!running_) {
    error = "sim backend must be running before pull_frames";
    return {};
  }

  if (duration < std::chrono::milliseconds::zero()) {
    error = "pull_frames duration cannot be negative";
    return {};
  }

  if (duration == std::chrono::milliseconds::zero()) {
    return {};
  }

  const std::uint32_t fps = ResolveFps(error);
  if (!error.empty()) {
    return {};
  }

  const auto duration_ms = duration.count();
  const std::uint64_t frame_count =
      static_cast<std::uint64_t>((duration_ms * static_cast<std::int64_t>(fps)) / 1000);

  std::vector<FrameSample> frames;
  frames.reserve(static_cast<std::size_t>(frame_count));

  if (frame_count == 0) {
    return frames;
  }

  const auto start_ts = std::chrono::system_clock::now();
  std::chrono::microseconds spacing_us =
      std::chrono::duration_cast<std::chrono::microseconds>(duration) /
      static_cast<std::int64_t>(frame_count);
  if (spacing_us < std::chrono::microseconds(1)) {
    spacing_us = std::chrono::microseconds(1);
  }

  for (std::uint64_t i = 0; i < frame_count; ++i) {
    FrameSample frame;
    frame.index = next_frame_index_++;
    frame.ts = start_ts + spacing_us * static_cast<std::int64_t>(i);
    frames.push_back(frame);
  }

  return frames;
}

std::uint32_t SimCameraBackend::ResolveFps(std::string& error) const {
  auto it = params_.find("fps");
  if (it == params_.end()) {
    return kDefaultFps;
  }

  std::uint32_t fps = 0;
  if (!ParsePositiveUInt32(it->second, fps)) {
    error = "invalid fps parameter value: " + it->second;
    return 0;
  }

  return fps;
}

} // namespace labops::backends::sim
