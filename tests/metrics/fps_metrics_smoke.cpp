#include "artifacts/metrics_writer.hpp"
#include "backends/camera_backend.hpp"
#include "metrics/fps.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertNear(double actual, double expected, double tolerance, std::string_view message) {
  if (std::abs(actual - expected) > tolerance) {
    std::cerr << message << " expected=" << expected << " actual=" << actual << '\n';
    std::abort();
  }
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  using labops::backends::FrameOutcome;
  using labops::backends::FrameSample;
  using labops::metrics::FpsReport;

  const auto base =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));
  const std::vector<FrameSample> frames = {
      {.frame_id = 0, .timestamp = base + std::chrono::milliseconds(1000), .size_bytes = 1024},
      {.frame_id = 1, .timestamp = base + std::chrono::milliseconds(1500), .size_bytes = 1024},
      {.frame_id = 2, .timestamp = base + std::chrono::milliseconds(2000), .size_bytes = 1024},
      {.frame_id = 3,
       .timestamp = base + std::chrono::milliseconds(2500),
       .size_bytes = 0,
       .dropped = true,
       .outcome = FrameOutcome::kDropped},
      {.frame_id = 5,
       .timestamp = base + std::chrono::milliseconds(2600),
       .size_bytes = 0,
       .dropped = true,
       .outcome = FrameOutcome::kTimeout},
      {.frame_id = 6,
       .timestamp = base + std::chrono::milliseconds(2700),
       .size_bytes = 256,
       .dropped = true,
       .outcome = FrameOutcome::kIncomplete},
      {.frame_id = 4, .timestamp = base + std::chrono::milliseconds(2800), .size_bytes = 1024},
  };

  FpsReport report;
  std::string error;
  if (!labops::metrics::ComputeFpsReport(frames, std::chrono::milliseconds(2000),
                                         std::chrono::milliseconds(1000), report, error)) {
    Fail("ComputeFpsReport failed: " + error);
  }

  if (report.received_frames_total != 4U) {
    Fail("unexpected received frame total");
  }
  if (report.frames_total != 7U) {
    Fail("unexpected total frame count");
  }
  if (report.dropped_frames_total != 3U) {
    Fail("unexpected dropped frame total");
  }
  if (report.dropped_generic_frames_total != 1U) {
    Fail("unexpected generic drop frame total");
  }
  if (report.timeout_frames_total != 1U) {
    Fail("unexpected timeout frame total");
  }
  if (report.incomplete_frames_total != 1U) {
    Fail("unexpected incomplete frame total");
  }
  AssertNear(report.drop_rate_percent, 42.8571428571, 1e-6, "unexpected drop rate percent");
  AssertNear(report.generic_drop_rate_percent, 14.2857142857, 1e-6,
             "unexpected generic drop rate percent");
  AssertNear(report.timeout_rate_percent, 14.2857142857, 1e-6, "unexpected timeout rate percent");
  AssertNear(report.incomplete_rate_percent, 14.2857142857, 1e-6,
             "unexpected incomplete rate percent");
  AssertNear(report.avg_fps, 2.0, 1e-9, "unexpected avg_fps");

  if (report.rolling_samples.size() != 4U) {
    Fail("unexpected rolling sample count");
  }
  AssertNear(report.rolling_samples[0].fps, 1.0, 1e-9, "rolling fps index 0 mismatch");
  AssertNear(report.rolling_samples[1].fps, 2.0, 1e-9, "rolling fps index 1 mismatch");
  AssertNear(report.rolling_samples[2].fps, 3.0, 1e-9, "rolling fps index 2 mismatch");
  AssertNear(report.rolling_samples[3].fps, 2.0, 1e-9, "rolling fps index 3 mismatch");

  if (report.inter_frame_interval_us.sample_count != 3U) {
    Fail("unexpected inter-frame interval sample count");
  }
  AssertNear(report.inter_frame_interval_us.min_us, 500000.0, 1e-9, "interval min mismatch");
  AssertNear(report.inter_frame_interval_us.avg_us, 600000.0, 1e-9, "interval avg mismatch");
  AssertNear(report.inter_frame_interval_us.p95_us, 800000.0, 1e-9, "interval p95 mismatch");

  if (report.inter_frame_jitter_us.sample_count != 3U) {
    Fail("unexpected inter-frame jitter sample count");
  }
  AssertNear(report.inter_frame_jitter_us.min_us, 100000.0, 1e-9, "jitter min mismatch");
  AssertNear(report.inter_frame_jitter_us.avg_us, 133333.333333, 1e-6, "jitter avg mismatch");
  AssertNear(report.inter_frame_jitter_us.p95_us, 200000.0, 1e-9, "jitter p95 mismatch");

  const fs::path out_dir = fs::temp_directory_path() / "labops-fps-metrics-smoke";
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  fs::path csv_path;
  if (!labops::artifacts::WriteMetricsCsv(report, out_dir, csv_path, error)) {
    Fail("WriteMetricsCsv failed: " + error);
  }

  fs::path json_path;
  if (!labops::artifacts::WriteMetricsJson(report, out_dir, json_path, error)) {
    Fail("WriteMetricsJson failed: " + error);
  }

  std::ifstream in(csv_path, std::ios::binary);
  if (!in) {
    Fail("failed to open metrics.csv");
  }
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  AssertContains(content, "metric,window_end_ms,window_ms,frames,fps");
  AssertContains(content, "avg_fps,,2000,4,2.000000");
  AssertContains(content, "drops_total,,,7,3");
  AssertContains(content, "drops_generic_total,,,7,1");
  AssertContains(content, "timeouts_total,,,7,1");
  AssertContains(content, "incomplete_total,,,7,1");
  AssertContains(content, "drop_rate_percent,,,7,42.857143");
  AssertContains(content, "generic_drop_rate_percent,,,7,14.285714");
  AssertContains(content, "timeout_rate_percent,,,7,14.285714");
  AssertContains(content, "incomplete_rate_percent,,,7,14.285714");
  AssertContains(content, "rolling_fps,");
  AssertContains(content, "inter_frame_interval_avg_us,,,3,600000.000000");
  AssertContains(content, "inter_frame_jitter_p95_us,,,3,200000.000000");

  std::ifstream json_in(json_path, std::ios::binary);
  if (!json_in) {
    Fail("failed to open metrics.json");
  }
  const std::string json_content((std::istreambuf_iterator<char>(json_in)),
                                 std::istreambuf_iterator<char>());
  AssertContains(json_content, "\"avg_fps\":2.000000");
  AssertContains(json_content, "\"dropped_generic_frames_total\":1");
  AssertContains(json_content, "\"timeout_frames_total\":1");
  AssertContains(json_content, "\"incomplete_frames_total\":1");
  AssertContains(json_content, "\"drop_rate_percent\":42.857143");
  AssertContains(json_content, "\"generic_drop_rate_percent\":14.285714");
  AssertContains(json_content, "\"timeout_rate_percent\":14.285714");
  AssertContains(json_content, "\"incomplete_rate_percent\":14.285714");
  AssertContains(json_content, "\"rolling_fps\":[");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "fps_metrics_smoke: ok\n";
  return 0;
}
