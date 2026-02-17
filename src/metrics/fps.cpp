#include "metrics/fps.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace labops::metrics {

namespace {

bool IsDropped(const backends::FrameSample& frame) {
  return frame.dropped.has_value() && frame.dropped.value();
}

backends::FrameOutcome ResolveOutcome(const backends::FrameSample& frame) {
  // Legacy fixtures may not set `outcome` yet. When dropped=true without an
  // explicit outcome, classify as generic dropped so historical behavior
  // remains stable while new categories roll out.
  if (frame.outcome == backends::FrameOutcome::kReceived && IsDropped(frame)) {
    return backends::FrameOutcome::kDropped;
  }
  return frame.outcome;
}

TimingStatsUs ComputeTimingStatsUs(std::vector<double> samples_us) {
  TimingStatsUs stats;
  if (samples_us.empty()) {
    return stats;
  }

  std::sort(samples_us.begin(), samples_us.end());
  stats.sample_count = static_cast<std::uint64_t>(samples_us.size());
  stats.min_us = samples_us.front();

  const double sum_us = std::accumulate(samples_us.begin(), samples_us.end(), 0.0);
  stats.avg_us = sum_us / static_cast<double>(samples_us.size());

  // Nearest-rank p95 approximation keeps behavior deterministic and simple.
  const std::size_t rank =
      static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(samples_us.size())));
  const std::size_t index = rank > 0U ? (rank - 1U) : 0U;
  stats.p95_us = samples_us[index];
  return stats;
}

} // namespace

bool ComputeFpsReport(const std::vector<backends::FrameSample>& frames,
                      const std::chrono::milliseconds avg_window,
                      const std::chrono::milliseconds rolling_window, FpsReport& report,
                      std::string& error) {
  if (avg_window <= std::chrono::milliseconds::zero()) {
    error = "avg fps window must be greater than 0";
    return false;
  }
  if (rolling_window <= std::chrono::milliseconds::zero()) {
    error = "rolling fps window must be greater than 0";
    return false;
  }

  std::vector<std::chrono::system_clock::time_point> received_timestamps;
  received_timestamps.reserve(frames.size());
  std::uint64_t dropped_frames_total = 0;
  std::uint64_t dropped_generic_frames_total = 0;
  std::uint64_t timeout_frames_total = 0;
  std::uint64_t incomplete_frames_total = 0;
  for (const auto& frame : frames) {
    const backends::FrameOutcome outcome = ResolveOutcome(frame);
    if (outcome == backends::FrameOutcome::kTimeout) {
      ++timeout_frames_total;
      ++dropped_frames_total;
      continue;
    }
    if (outcome == backends::FrameOutcome::kIncomplete) {
      ++incomplete_frames_total;
      ++dropped_frames_total;
      continue;
    }
    if (outcome == backends::FrameOutcome::kDropped || IsDropped(frame)) {
      ++dropped_generic_frames_total;
      ++dropped_frames_total;
      continue;
    }
    received_timestamps.push_back(frame.timestamp);
  }

  std::sort(received_timestamps.begin(), received_timestamps.end());

  report = FpsReport{};
  report.avg_window = avg_window;
  report.rolling_window = rolling_window;
  report.frames_total = static_cast<std::uint64_t>(frames.size());
  report.received_frames_total = static_cast<std::uint64_t>(received_timestamps.size());
  report.dropped_frames_total = dropped_frames_total;
  report.dropped_generic_frames_total = dropped_generic_frames_total;
  report.timeout_frames_total = timeout_frames_total;
  report.incomplete_frames_total = incomplete_frames_total;
  if (report.frames_total > 0U) {
    report.drop_rate_percent = (static_cast<double>(report.dropped_frames_total) * 100.0) /
                               static_cast<double>(report.frames_total);
    report.generic_drop_rate_percent =
        (static_cast<double>(report.dropped_generic_frames_total) * 100.0) /
        static_cast<double>(report.frames_total);
    report.timeout_rate_percent = (static_cast<double>(report.timeout_frames_total) * 100.0) /
                                  static_cast<double>(report.frames_total);
    report.incomplete_rate_percent = (static_cast<double>(report.incomplete_frames_total) * 100.0) /
                                     static_cast<double>(report.frames_total);
  }

  const double avg_window_seconds = static_cast<double>(avg_window.count()) / 1000.0;
  report.avg_fps = static_cast<double>(report.received_frames_total) / avg_window_seconds;

  if (received_timestamps.empty()) {
    return true;
  }

  const double rolling_window_seconds = static_cast<double>(rolling_window.count()) / 1000.0;

  // Two-pointer sliding window keeps rolling computation linear and stable.
  std::size_t left = 0;
  report.rolling_samples.reserve(received_timestamps.size());
  for (std::size_t right = 0; right < received_timestamps.size(); ++right) {
    const auto window_start = received_timestamps[right] - rolling_window;
    while (left < right && received_timestamps[left] < window_start) {
      ++left;
    }

    const std::uint64_t count =
        static_cast<std::uint64_t>(right - left + static_cast<std::size_t>(1));
    const double fps = static_cast<double>(count) / rolling_window_seconds;
    report.rolling_samples.push_back(
        {.window_end = received_timestamps[right], .frames_in_window = count, .fps = fps});
  }

  if (received_timestamps.size() >= 2U) {
    std::vector<double> intervals_us;
    intervals_us.reserve(received_timestamps.size() - 1U);
    for (std::size_t i = 1; i < received_timestamps.size(); ++i) {
      const auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                received_timestamps[i] - received_timestamps[i - 1])
                                .count();
      intervals_us.push_back(static_cast<double>(delta_us));
    }

    report.inter_frame_interval_us = ComputeTimingStatsUs(intervals_us);

    std::vector<double> jitter_us;
    jitter_us.reserve(intervals_us.size());
    for (double interval_us : intervals_us) {
      jitter_us.push_back(std::abs(interval_us - report.inter_frame_interval_us.avg_us));
    }
    report.inter_frame_jitter_us = ComputeTimingStatsUs(jitter_us);
  }

  return true;
}

} // namespace labops::metrics
