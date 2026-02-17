#include "metrics/anomalies.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <vector>

namespace labops::metrics {

namespace {

constexpr std::size_t kMaxAnomalyHighlights = 3U;

std::string FormatDouble(const double value, const int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

double Median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t mid = values.size() / 2U;
  if ((values.size() % 2U) == 0U) {
    return (values[mid - 1U] + values[mid]) * 0.5;
  }
  return values[mid];
}

bool TryDetectResendSpike(const FpsReport& report, const std::uint32_t configured_fps,
                          std::string& message) {
  if (report.rolling_samples.size() < 10U) {
    return false;
  }

  std::vector<double> fps_values;
  fps_values.reserve(report.rolling_samples.size());
  double peak_fps = 0.0;
  for (const auto& sample : report.rolling_samples) {
    fps_values.push_back(sample.fps);
    peak_fps = std::max(peak_fps, sample.fps);
  }

  const double median_fps = Median(fps_values);
  if (median_fps <= 0.0) {
    return false;
  }

  const double peak_vs_median = peak_fps / median_fps;
  const double configured_floor =
      configured_fps > 0U ? static_cast<double>(configured_fps) * 1.40 : 0.0;
  const bool spike_by_shape = peak_vs_median >= 1.70;
  const bool spike_by_config = configured_fps > 0U && peak_fps >= configured_floor;
  const bool corroborated_by_fault_signals =
      report.dropped_frames_total > 0U ||
      (report.inter_frame_jitter_us.sample_count > 0U &&
       report.inter_frame_jitter_us.avg_us > 0.0 &&
       report.inter_frame_jitter_us.p95_us >= report.inter_frame_jitter_us.avg_us * 2.50);

  if ((!spike_by_shape && !spike_by_config) || !corroborated_by_fault_signals) {
    return false;
  }

  message = "Resend spike detected: rolling FPS peak " + FormatDouble(peak_fps, 2) +
            " exceeded stable median " + FormatDouble(median_fps, 2) + " (" +
            FormatDouble(peak_vs_median, 2) + "x).";
  return true;
}

bool TryDetectJitterCliff(const FpsReport& report, const std::uint32_t configured_fps,
                          std::string& message) {
  if (report.inter_frame_jitter_us.sample_count < 10U ||
      report.inter_frame_jitter_us.avg_us <= 0.0) {
    return false;
  }

  const double jitter_p95 = report.inter_frame_jitter_us.p95_us;
  const double jitter_avg = report.inter_frame_jitter_us.avg_us;
  const double p95_vs_avg = jitter_p95 / jitter_avg;
  const double expected_interval_us =
      configured_fps > 0U ? (1'000'000.0 / static_cast<double>(configured_fps)) : 0.0;
  const double absolute_floor_us = std::max(2'000.0, expected_interval_us * 0.15);

  if (p95_vs_avg < 4.00 || jitter_p95 < absolute_floor_us) {
    return false;
  }

  message = "Jitter cliff detected: jitter p95 " + FormatDouble(jitter_p95, 1) + "us is " +
            FormatDouble(p95_vs_avg, 2) + "x avg jitter " + FormatDouble(jitter_avg, 1) + "us.";
  return true;
}

bool TryDetectPeriodicStall(const FpsReport& report, const std::uint32_t configured_fps,
                            std::string& message) {
  if (configured_fps == 0U || report.rolling_samples.size() < 20U ||
      report.rolling_window <= std::chrono::milliseconds::zero()) {
    return false;
  }

  const double stall_fps_threshold = static_cast<double>(configured_fps) * 0.35;
  const auto rolling_window_ms = report.rolling_window.count();
  const auto min_event_separation_ms = std::max<std::int64_t>(rolling_window_ms / 2, 200);

  std::vector<std::int64_t> stall_events_ms;
  for (const auto& sample : report.rolling_samples) {
    if (sample.fps > stall_fps_threshold) {
      continue;
    }

    const auto ts_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(sample.window_end.time_since_epoch())
            .count();
    if (!stall_events_ms.empty() && (ts_ms - stall_events_ms.back()) < min_event_separation_ms) {
      continue;
    }
    stall_events_ms.push_back(ts_ms);
  }

  if (stall_events_ms.size() < 3U) {
    return false;
  }

  std::vector<double> intervals_ms;
  intervals_ms.reserve(stall_events_ms.size() - 1U);
  for (std::size_t i = 1; i < stall_events_ms.size(); ++i) {
    intervals_ms.push_back(static_cast<double>(stall_events_ms[i] - stall_events_ms[i - 1U]));
  }

  const double mean_interval_ms =
      std::accumulate(intervals_ms.begin(), intervals_ms.end(), 0.0) / intervals_ms.size();
  if (mean_interval_ms < static_cast<double>(rolling_window_ms)) {
    return false;
  }

  const auto [min_it, max_it] = std::minmax_element(intervals_ms.begin(), intervals_ms.end());
  const double spread_ms = *max_it - *min_it;
  if (spread_ms > mean_interval_ms * 0.35) {
    return false;
  }

  message = "Periodic stall detected: low-throughput valleys repeat roughly every " +
            FormatDouble(mean_interval_ms, 0) + "ms (" + std::to_string(stall_events_ms.size()) +
            " events).";
  return true;
}

void AddLegacySignals(const FpsReport& report, const std::uint32_t configured_fps,
                      std::vector<std::string>& anomalies) {
  if (report.received_frames_total == 0U) {
    anomalies.push_back("No frames were received during the run.");
  }

  if (report.dropped_frames_total > 0U) {
    std::string breakdown = "Dropped " + std::to_string(report.dropped_frames_total) + " of " +
                            std::to_string(report.frames_total) + " frames (" +
                            FormatDouble(report.drop_rate_percent, 2) + "%).";
    breakdown += " breakdown: generic=" + std::to_string(report.dropped_generic_frames_total);
    breakdown += ", timeout=" + std::to_string(report.timeout_frames_total);
    breakdown += ", incomplete=" + std::to_string(report.incomplete_frames_total) + ".";
    anomalies.push_back(std::move(breakdown));
  }

  if (configured_fps == 0U) {
    return;
  }

  const double expected_interval_us = 1'000'000.0 / static_cast<double>(configured_fps);
  const double avg_fps_floor = static_cast<double>(configured_fps) * 0.90;
  if (report.avg_fps + 1e-9 < avg_fps_floor) {
    anomalies.push_back("Average FPS " + FormatDouble(report.avg_fps, 2) +
                        " is below 90% of configured FPS " + std::to_string(configured_fps) + ".");
  }

  if (report.inter_frame_interval_us.sample_count > 0U &&
      report.inter_frame_interval_us.p95_us > expected_interval_us * 1.50) {
    anomalies.push_back(
        "Inter-frame interval p95 " + FormatDouble(report.inter_frame_interval_us.p95_us, 1) +
        "us is >150% of expected cadence " + FormatDouble(expected_interval_us, 1) + "us.");
  }

  if (report.inter_frame_jitter_us.sample_count > 0U &&
      report.inter_frame_jitter_us.p95_us > expected_interval_us * 0.50) {
    anomalies.push_back(
        "Inter-frame jitter p95 " + FormatDouble(report.inter_frame_jitter_us.p95_us, 1) +
        "us is high relative to expected cadence " + FormatDouble(expected_interval_us, 1) + "us.");
  }
}

} // namespace

std::vector<std::string>
BuildAnomalyHighlights(const FpsReport& report, const std::uint32_t configured_fps,
                       const std::vector<std::string>& threshold_failures) {
  std::vector<std::string> anomalies;

  // Named heuristics first so the top-anomaly section surfaces recognizable
  // patterns before generic threshold text.
  std::string heuristic_message;
  if (TryDetectResendSpike(report, configured_fps, heuristic_message)) {
    anomalies.push_back(heuristic_message);
  }
  if (TryDetectJitterCliff(report, configured_fps, heuristic_message)) {
    anomalies.push_back(heuristic_message);
  }
  if (TryDetectPeriodicStall(report, configured_fps, heuristic_message)) {
    anomalies.push_back(heuristic_message);
  }

  AddLegacySignals(report, configured_fps, anomalies);

  for (const auto& failure : threshold_failures) {
    anomalies.push_back("Threshold violation: " + failure);
  }

  if (anomalies.empty()) {
    anomalies.push_back("No notable anomalies detected by current heuristics.");
  }

  std::vector<std::string> deduped;
  deduped.reserve(anomalies.size());
  std::set<std::string> seen;
  for (const auto& anomaly : anomalies) {
    if (!seen.insert(anomaly).second) {
      continue;
    }
    deduped.push_back(anomaly);
  }

  if (deduped.size() > kMaxAnomalyHighlights) {
    deduped.resize(kMaxAnomalyHighlights);
  }
  return deduped;
}

} // namespace labops::metrics
