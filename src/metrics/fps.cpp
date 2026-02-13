#include "metrics/fps.hpp"

#include <algorithm>

namespace labops::metrics {

namespace {

bool IsDropped(const backends::FrameSample& frame) {
  return frame.dropped.has_value() && frame.dropped.value();
}

} // namespace

bool ComputeFpsReport(const std::vector<backends::FrameSample>& frames,
                      const std::chrono::milliseconds avg_window,
                      const std::chrono::milliseconds rolling_window,
                      FpsReport& report,
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
  for (const auto& frame : frames) {
    if (!IsDropped(frame)) {
      received_timestamps.push_back(frame.timestamp);
    }
  }

  std::sort(received_timestamps.begin(), received_timestamps.end());

  report = FpsReport{};
  report.avg_window = avg_window;
  report.rolling_window = rolling_window;
  report.received_frames_total = static_cast<std::uint64_t>(received_timestamps.size());

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

  return true;
}

} // namespace labops::metrics
