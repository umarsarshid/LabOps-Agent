#pragma once

#include <chrono>
#include <cstdint>
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

// RunInfo combines run identity, immutable config, and lifecycle timing into
// the minimal contract required to explain what executed and when.
struct RunInfo {
  std::string run_id;
  RunConfig config;
  RunTimestamps timestamps;
};

// JSON serializers for stable artifact emission and test assertions. These
// return canonical key ordering to keep diffs and snapshots predictable.
std::string ToJson(const RunConfig& run_config);
std::string ToJson(const RunInfo& run_info);

} // namespace labops::core::schema
