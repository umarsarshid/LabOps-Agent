#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace labops::core::schema {

// Immutable inputs that define how a run should execute. These fields are
// expected to be present for every run and are critical for reproducibility.
struct RunConfig {
  std::string scenario_id;
  std::string backend;
  std::uint64_t seed = 0;
  std::chrono::milliseconds duration{0};
};

// Lifecycle timestamps captured for every run. Keeping these grouped makes
// timeline handling explicit and avoids loosely related timestamp fields.
struct RunTimestamps {
  std::chrono::system_clock::time_point created_at{};
  std::chrono::system_clock::time_point started_at{};
  std::chrono::system_clock::time_point finished_at{};
};

// Real-device metadata captured when a run resolves a concrete physical camera.
//
// This is optional because sim runs (and early-failure real runs without device
// resolution) may not have concrete hardware identity/version details.
struct RealDeviceMetadata {
  std::string model;
  std::string serial;
  std::string transport;
  std::optional<std::string> user_id;
  std::optional<std::string> firmware_version;
  std::optional<std::string> sdk_version;
};

// RunInfo combines run identity, immutable config, and lifecycle timing into
// the minimal contract required to explain what executed and when.
struct RunInfo {
  std::string run_id;
  RunConfig config;
  std::optional<RealDeviceMetadata> real_device;
  RunTimestamps timestamps;
};

// JSON serializers for stable artifact emission and test assertions. These
// return canonical key ordering to keep diffs and snapshots predictable.
std::string ToJson(const RunConfig& run_config);
std::string ToJson(const RealDeviceMetadata& real_device);
std::string ToJson(const RunInfo& run_info);

} // namespace labops::core::schema
