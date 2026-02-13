#include "backends/camera_backend.hpp"
#include "metrics/csv_writer.hpp"
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
  using labops::backends::FrameSample;
  using labops::metrics::FpsReport;

  const auto base = std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));
  const std::vector<FrameSample> frames = {
      {.frame_id = 0, .timestamp = base + std::chrono::milliseconds(1000), .size_bytes = 1024},
      {.frame_id = 1, .timestamp = base + std::chrono::milliseconds(1500), .size_bytes = 1024},
      {.frame_id = 2, .timestamp = base + std::chrono::milliseconds(2000), .size_bytes = 1024},
      {.frame_id = 3,
       .timestamp = base + std::chrono::milliseconds(2500),
       .size_bytes = 0,
       .dropped = true},
      {.frame_id = 4, .timestamp = base + std::chrono::milliseconds(2800), .size_bytes = 1024},
  };

  FpsReport report;
  std::string error;
  if (!labops::metrics::ComputeFpsReport(frames,
                                         std::chrono::milliseconds(2000),
                                         std::chrono::milliseconds(1000),
                                         report,
                                         error)) {
    Fail("ComputeFpsReport failed: " + error);
  }

  if (report.received_frames_total != 4U) {
    Fail("unexpected received frame total");
  }
  AssertNear(report.avg_fps, 2.0, 1e-9, "unexpected avg_fps");

  if (report.rolling_samples.size() != 4U) {
    Fail("unexpected rolling sample count");
  }
  AssertNear(report.rolling_samples[0].fps, 1.0, 1e-9, "rolling fps index 0 mismatch");
  AssertNear(report.rolling_samples[1].fps, 2.0, 1e-9, "rolling fps index 1 mismatch");
  AssertNear(report.rolling_samples[2].fps, 3.0, 1e-9, "rolling fps index 2 mismatch");
  AssertNear(report.rolling_samples[3].fps, 2.0, 1e-9, "rolling fps index 3 mismatch");

  const fs::path out_dir = fs::temp_directory_path() / "labops-fps-metrics-smoke";
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  fs::path written_path;
  if (!labops::metrics::WriteFpsMetricsCsv(report, out_dir, written_path, error)) {
    Fail("WriteFpsMetricsCsv failed: " + error);
  }

  std::ifstream in(written_path, std::ios::binary);
  if (!in) {
    Fail("failed to open metrics.csv");
  }
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  AssertContains(content, "metric,window_end_ms,window_ms,frames,fps");
  AssertContains(content, "avg_fps,,2000,4,2.000000");
  AssertContains(content, "rolling_fps,");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "fps_metrics_smoke: ok\n";
  return 0;
}
