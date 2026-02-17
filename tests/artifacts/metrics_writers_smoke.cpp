#include "artifacts/metrics_writer.hpp"
#include "metrics/fps.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
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
  labops::metrics::FpsReport report;
  report.avg_window = std::chrono::milliseconds(3000);
  report.rolling_window = std::chrono::milliseconds(1000);
  report.frames_total = 90;
  report.received_frames_total = 81;
  report.dropped_frames_total = 9;
  report.dropped_generic_frames_total = 4;
  report.timeout_frames_total = 3;
  report.incomplete_frames_total = 2;
  report.drop_rate_percent = 10.0;
  report.generic_drop_rate_percent = 4.444444;
  report.timeout_rate_percent = 3.333333;
  report.incomplete_rate_percent = 2.222222;
  report.avg_fps = 27.0;
  report.inter_frame_interval_us = {
      .sample_count = 80,
      .min_us = 16000.0,
      .avg_us = 16666.0,
      .p95_us = 17000.0,
  };
  report.inter_frame_jitter_us = {
      .sample_count = 80,
      .min_us = 10.0,
      .avg_us = 120.0,
      .p95_us = 400.0,
  };

  const auto base_ts =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));
  report.rolling_samples.push_back({.window_end = base_ts, .frames_in_window = 25, .fps = 25.0});
  report.rolling_samples.push_back({.window_end = base_ts + std::chrono::milliseconds(1000),
                                    .frames_in_window = 27,
                                    .fps = 27.0});

  const fs::path out_dir = fs::temp_directory_path() / "labops-metrics-writers-smoke";
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  std::string error;
  fs::path csv_path;
  if (!labops::artifacts::WriteMetricsCsv(report, out_dir, csv_path, error)) {
    Fail("WriteMetricsCsv failed: " + error);
  }

  fs::path json_path;
  if (!labops::artifacts::WriteMetricsJson(report, out_dir, json_path, error)) {
    Fail("WriteMetricsJson failed: " + error);
  }

  std::ifstream csv_input(csv_path, std::ios::binary);
  if (!csv_input) {
    Fail("failed to open metrics.csv");
  }
  const std::string csv_content((std::istreambuf_iterator<char>(csv_input)),
                                std::istreambuf_iterator<char>());
  AssertContains(csv_content, "avg_fps,,3000,81,27.000000");
  AssertContains(csv_content, "drops_total,,,90,9");
  AssertContains(csv_content, "drops_generic_total,,,90,4");
  AssertContains(csv_content, "timeouts_total,,,90,3");
  AssertContains(csv_content, "incomplete_total,,,90,2");
  AssertContains(csv_content, "drop_rate_percent,,,90,10.000000");
  AssertContains(csv_content, "generic_drop_rate_percent,,,90,4.444444");
  AssertContains(csv_content, "timeout_rate_percent,,,90,3.333333");
  AssertContains(csv_content, "incomplete_rate_percent,,,90,2.222222");

  std::ifstream json_input(json_path, std::ios::binary);
  if (!json_input) {
    Fail("failed to open metrics.json");
  }
  const std::string json_content((std::istreambuf_iterator<char>(json_input)),
                                 std::istreambuf_iterator<char>());
  AssertContains(json_content, "\"avg_window_ms\":3000");
  AssertContains(json_content, "\"received_frames_total\":81");
  AssertContains(json_content, "\"dropped_frames_total\":9");
  AssertContains(json_content, "\"dropped_generic_frames_total\":4");
  AssertContains(json_content, "\"timeout_frames_total\":3");
  AssertContains(json_content, "\"incomplete_frames_total\":2");
  AssertContains(json_content, "\"drop_rate_percent\":10.000000");
  AssertContains(json_content, "\"generic_drop_rate_percent\":4.444444");
  AssertContains(json_content, "\"timeout_rate_percent\":3.333333");
  AssertContains(json_content, "\"incomplete_rate_percent\":2.222222");
  AssertContains(json_content, "\"rolling_fps\":[");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "metrics_writers_smoke: ok\n";
  return 0;
}
