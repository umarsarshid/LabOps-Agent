#include "backends/sim/sim_camera_backend.hpp"

#include <charconv>

namespace labops::backends::sim {

namespace {

constexpr std::uint32_t kDefaultFps = 30;
constexpr std::uint32_t kDefaultJitterUs = 0;
constexpr std::uint32_t kDefaultFrameSizeBytes = 1'048'576;
constexpr std::uint32_t kDefaultDropEveryN = 0;
constexpr std::uint64_t kDefaultSeed = 1;
constexpr std::uint64_t kSplitMixIncrement = 0x9e3779b97f4a7c15ULL;

bool ParseUInt32(const std::string& text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint32_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return false;
  }

  value = parsed;
  return true;
}

bool ParsePositiveUInt32(const std::string& text, std::uint32_t& value) {
  if (!ParseUInt32(text, value)) {
    return false;
  }
  return value > 0;
}

bool ParseUInt64(const std::string& text, std::uint64_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint64_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return false;
  }

  value = parsed;
  return true;
}

std::uint64_t SplitMix64(std::uint64_t value) {
  std::uint64_t state = value + kSplitMixIncrement;
  state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
  state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
  return state ^ (state >> 31);
}

std::int64_t DeterministicJitterUs(std::uint64_t seed, std::uint64_t frame_id,
                                   std::uint32_t max_abs_jitter_us) {
  if (max_abs_jitter_us == 0) {
    return 0;
  }

  const std::uint64_t mixed = SplitMix64(seed ^ (frame_id * kSplitMixIncrement));
  const std::uint64_t span = static_cast<std::uint64_t>(max_abs_jitter_us) * 2ULL + 1ULL;
  const std::int64_t offset = static_cast<std::int64_t>(mixed % span);
  return offset - static_cast<std::int64_t>(max_abs_jitter_us);
}

} // namespace

SimCameraBackend::SimCameraBackend() {
  params_ = {
      {"backend", "sim"},
      {"fps", std::to_string(kDefaultFps)},
      {"jitter_us", std::to_string(kDefaultJitterUs)},
      {"frame_size_bytes", std::to_string(kDefaultFrameSizeBytes)},
      {"drop_every_n", std::to_string(kDefaultDropEveryN)},
      {"seed", std::to_string(kDefaultSeed)},
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
  next_frame_id_ = 0;
  stream_start_ts_ = std::chrono::system_clock::now();
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
  const std::uint32_t jitter_us = ResolveJitterUs(error);
  const std::uint32_t frame_size_bytes = ResolveFrameSizeBytes(error);
  const std::uint64_t seed = ResolveSeed(error);
  const std::uint32_t drop_every_n = ResolveDropEveryN(error);
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

  std::chrono::nanoseconds frame_period_ns(1'000'000'000LL / static_cast<std::int64_t>(fps));
  if (frame_period_ns < std::chrono::nanoseconds(1)) {
    frame_period_ns = std::chrono::nanoseconds(1);
  }

  for (std::uint64_t i = 0; i < frame_count; ++i) {
    const std::uint64_t frame_id = next_frame_id_++;
    const auto nominal_ts =
        stream_start_ts_ + frame_period_ns * static_cast<std::int64_t>(frame_id);
    const auto jittered_ts =
        nominal_ts + std::chrono::microseconds(DeterministicJitterUs(seed, frame_id, jitter_us));

    FrameSample frame;
    frame.frame_id = frame_id;
    frame.timestamp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(jittered_ts);
    if (!frames.empty() && frame.timestamp <= frames.back().timestamp) {
      frame.timestamp = frames.back().timestamp + std::chrono::microseconds(1);
    }

    const bool is_dropped =
        (drop_every_n > 0U) &&
        (((frame.frame_id + 1U) % static_cast<std::uint64_t>(drop_every_n)) == 0U);
    frame.size_bytes = is_dropped ? 0U : frame_size_bytes;
    if (is_dropped) {
      frame.dropped = true;
    }

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

std::uint32_t SimCameraBackend::ResolveJitterUs(std::string& error) const {
  auto it = params_.find("jitter_us");
  if (it == params_.end()) {
    return kDefaultJitterUs;
  }

  std::uint32_t jitter_us = 0;
  if (!ParseUInt32(it->second, jitter_us)) {
    error = "invalid jitter_us parameter value: " + it->second;
    return 0;
  }

  return jitter_us;
}

std::uint32_t SimCameraBackend::ResolveFrameSizeBytes(std::string& error) const {
  auto it = params_.find("frame_size_bytes");
  if (it == params_.end()) {
    return kDefaultFrameSizeBytes;
  }

  std::uint32_t frame_size_bytes = 0;
  if (!ParsePositiveUInt32(it->second, frame_size_bytes)) {
    error = "invalid frame_size_bytes parameter value: " + it->second;
    return 0;
  }

  return frame_size_bytes;
}

std::uint64_t SimCameraBackend::ResolveSeed(std::string& error) const {
  auto it = params_.find("seed");
  if (it == params_.end()) {
    return kDefaultSeed;
  }

  std::uint64_t seed = 0;
  if (!ParseUInt64(it->second, seed)) {
    error = "invalid seed parameter value: " + it->second;
    return 0;
  }

  return seed;
}

std::uint32_t SimCameraBackend::ResolveDropEveryN(std::string& error) const {
  auto it = params_.find("drop_every_n");
  if (it == params_.end()) {
    return kDefaultDropEveryN;
  }

  std::uint32_t drop_every_n = 0;
  if (!ParseUInt32(it->second, drop_every_n)) {
    error = "invalid drop_every_n parameter value: " + it->second;
    return 0;
  }

  return drop_every_n;
}

} // namespace labops::backends::sim
