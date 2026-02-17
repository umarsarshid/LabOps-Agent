#include "artifacts/html_report_writer.hpp"
#include "core/schema/run_contract.hpp"
#include "metrics/fps.hpp"

#include <chrono>
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

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  const auto base_time =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));

  labops::core::schema::RunInfo run_info;
  run_info.run_id = "run-html-smoke";
  run_info.config.scenario_id = "html_smoke";
  run_info.config.backend = "sim";
  run_info.config.seed = 42;
  run_info.config.duration = std::chrono::milliseconds(1500);
  run_info.timestamps.created_at = base_time;
  run_info.timestamps.started_at = base_time + std::chrono::milliseconds(10);
  run_info.timestamps.finished_at = base_time + std::chrono::milliseconds(1510);

  labops::metrics::FpsReport report;
  report.avg_window = std::chrono::milliseconds(1500);
  report.rolling_window = std::chrono::milliseconds(1000);
  report.frames_total = 40;
  report.received_frames_total = 35;
  report.dropped_frames_total = 5;
  report.dropped_generic_frames_total = 2;
  report.timeout_frames_total = 2;
  report.incomplete_frames_total = 1;
  report.drop_rate_percent = 12.5;
  report.generic_drop_rate_percent = 5.0;
  report.timeout_rate_percent = 5.0;
  report.incomplete_rate_percent = 2.5;
  report.avg_fps = 23.3;
  report.inter_frame_interval_us = {
      .sample_count = 34,
      .min_us = 39000.0,
      .avg_us = 42000.0,
      .p95_us = 47000.0,
  };
  report.inter_frame_jitter_us = {
      .sample_count = 34,
      .min_us = 120.0,
      .avg_us = 250.0,
      .p95_us = 730.0,
  };
  report.rolling_samples.push_back({
      .window_end = base_time + std::chrono::milliseconds(1000),
      .frames_in_window = 22,
      .fps = 22.0,
  });
  report.rolling_samples.push_back({
      .window_end = base_time + std::chrono::milliseconds(1500),
      .frames_in_window = 24,
      .fps = 24.0,
  });

  const std::vector<std::string> threshold_failures = {
      "avg_fps actual=23.3 below minimum=25.0",
      "drop_rate_percent actual=12.5 exceeds maximum=10.0",
  };
  const std::vector<std::string> top_anomalies = {
      "Average FPS dropped below expected target.",
      "Drop rate exceeded expected threshold.",
  };

  const fs::path out_dir = fs::temp_directory_path() / "labops-html-report-smoke";
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  std::string error;
  fs::path written_path;
  if (!labops::artifacts::WriteRunSummaryHtml(run_info, report, /*configured_fps=*/30,
                                              /*thresholds_passed=*/false, threshold_failures,
                                              top_anomalies, out_dir, written_path, error)) {
    Fail("WriteRunSummaryHtml failed: " + error);
  }

  if (!fs::exists(written_path) || !fs::is_regular_file(written_path)) {
    Fail("report.html was not created");
  }

  std::ifstream input(written_path, std::ios::binary);
  if (!input) {
    Fail("failed to open report.html");
  }
  const std::string html((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

  AssertContains(html, "<title>LabOps Run Report</title>");
  AssertContains(html, "<h2>Key Metrics</h2>");
  AssertContains(html, "<h2>Diffs (Actual vs Expected)</h2>");
  AssertContains(html, "<h2>Rolling FPS Samples</h2>");
  AssertContains(html, "<h2>Threshold Checks</h2>");
  AssertContains(html, "<h2>Top Anomalies</h2>");
  AssertContains(html, "run-html-smoke");
  AssertContains(html, "drop_rate_percent");
  AssertContains(html, "generic_drop_rate_percent");
  AssertContains(html, "timeout_rate_percent");
  AssertContains(html, "incomplete_rate_percent");
  AssertContains(html, "window_end_epoch_ms");
  AssertContains(html, "avg_fps actual=23.3 below minimum=25.0");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "html_report_writer_smoke: ok\n";
  return 0;
}
