#pragma once

#include "backends/camera_backend.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace labops::metrics {

// One rolling FPS measurement at a specific window end timestamp.
struct RollingFpsSample {
  std::chrono::system_clock::time_point window_end{};
  std::uint64_t frames_in_window = 0;
  double fps = 0.0;
};

// Summary stats for microsecond-based timing distributions.
struct TimingStatsUs {
  std::uint64_t sample_count = 0;
  double min_us = 0.0;
  double avg_us = 0.0;
  double p95_us = 0.0;
};

// FPS report emitted for a run.
//
// - `avg_fps` is computed over the caller-provided run window.
// - `rolling_samples` are computed over a fixed rolling window and include one
//   sample per received frame.
// - `inter_frame_interval_us` summarizes deltas between consecutive received
//   frame timestamps.
// - `inter_frame_jitter_us` summarizes absolute deviation from average
//   inter-frame interval.
struct FpsReport {
  std::chrono::milliseconds avg_window{0};
  std::chrono::milliseconds rolling_window{0};
  std::uint64_t received_frames_total = 0;
  double avg_fps = 0.0;
  std::vector<RollingFpsSample> rolling_samples;
  TimingStatsUs inter_frame_interval_us;
  TimingStatsUs inter_frame_jitter_us;
};

// Computes average and rolling FPS using only received (non-dropped) frames.
//
// Contract:
// - `avg_window` and `rolling_window` must be > 0.
// - `frames` may arrive in any order; timestamp ordering is normalized.
// - dropped frames are excluded from FPS numerators.
// - returns false and populates `error` on invalid inputs.
bool ComputeFpsReport(const std::vector<backends::FrameSample>& frames,
                      std::chrono::milliseconds avg_window,
                      std::chrono::milliseconds rolling_window,
                      FpsReport& report,
                      std::string& error);

} // namespace labops::metrics
