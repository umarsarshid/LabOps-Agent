#include "metrics/anomalies.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

bool ContainsSubstring(const std::vector<std::string>& values, std::string_view token) {
  for (const auto& value : values) {
    if (value.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  labops::metrics::FpsReport report;
  report.rolling_window = std::chrono::milliseconds(1000);
  report.frames_total = 900;
  report.received_frames_total = 780;
  report.dropped_frames_total = 120;
  report.drop_rate_percent = 13.333333;
  report.avg_fps = 26.0;

  report.inter_frame_jitter_us = {
      .sample_count = 250,
      .min_us = 80.0,
      .avg_us = 900.0,
      .p95_us = 5200.0,
  };
  report.inter_frame_interval_us = {
      .sample_count = 250,
      .min_us = 29000.0,
      .avg_us = 38000.0,
      .p95_us = 62000.0,
  };

  const auto base_ts =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));
  for (std::size_t i = 0; i < 35U; ++i) {
    const auto ts = base_ts + std::chrono::milliseconds(static_cast<std::int64_t>(i) * 200);
    double fps = 30.0;

    // Seed periodic low-throughput valleys every ~2000ms.
    if (i == 5U || i == 15U || i == 25U) {
      fps = 5.0;
    }
    // Seed burst recovery spikes shortly after valleys.
    if (i == 6U || i == 16U || i == 26U) {
      fps = 58.0;
    }

    report.rolling_samples.push_back({
        .window_end = ts,
        .frames_in_window = static_cast<std::uint64_t>(fps),
        .fps = fps,
    });
  }

  const std::vector<std::string> anomalies =
      labops::metrics::BuildAnomalyHighlights(report, /*configured_fps=*/30, {});

  if (anomalies.empty()) {
    Fail("expected anomalies but list was empty");
  }
  if (!ContainsSubstring(anomalies, "Resend spike")) {
    Fail("expected resend spike heuristic anomaly");
  }
  if (!ContainsSubstring(anomalies, "Jitter cliff")) {
    Fail("expected jitter cliff heuristic anomaly");
  }
  if (!ContainsSubstring(anomalies, "Periodic stall")) {
    Fail("expected periodic stall heuristic anomaly");
  }

  std::cout << "anomaly_detection_smoke: ok\n";
  return 0;
}
