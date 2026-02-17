#include "artifacts/metrics_writer.hpp"
#include "backends/camera_backend.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "metrics/fps.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

labops::metrics::FpsReport RunWithDropEveryN(std::uint32_t drop_every_n,
                                             std::chrono::milliseconds duration,
                                             std::uint32_t fps) {
  using labops::backends::ICameraBackend;
  using labops::backends::sim::SimCameraBackend;
  using labops::backends::sim::SimScenarioConfig;

  std::unique_ptr<ICameraBackend> backend = std::make_unique<SimCameraBackend>();
  std::string error;
  if (!backend->Connect(error)) {
    Fail("backend connect failed: " + error);
  }

  SimScenarioConfig config;
  config.fps = fps;
  config.jitter_us = 0;
  config.seed = 9001;
  config.frame_size_bytes = 4096;
  config.drop_every_n = drop_every_n;
  config.faults.drop_percent = 0;
  config.faults.burst_drop = 0;
  config.faults.reorder = 0;
  if (!labops::backends::sim::ApplyScenarioConfig(*backend, config, error)) {
    Fail("scenario apply failed: " + error);
  }

  if (!backend->Start(error)) {
    Fail("backend start failed: " + error);
  }

  const std::vector<labops::backends::FrameSample> frames = backend->PullFrames(duration, error);
  if (!error.empty()) {
    Fail("pull_frames failed: " + error);
  }

  if (!backend->Stop(error)) {
    Fail("backend stop failed: " + error);
  }

  labops::metrics::FpsReport report;
  if (!labops::metrics::ComputeFpsReport(frames, duration, std::chrono::milliseconds(1000), report,
                                         error)) {
    Fail("ComputeFpsReport failed: " + error);
  }

  return report;
}

} // namespace

int main() {
  const std::chrono::milliseconds duration(2000);
  constexpr std::uint32_t kFps = 40;
  constexpr std::uint32_t kDropEveryN = 5;

  const auto baseline = RunWithDropEveryN(0, duration, kFps);
  const auto injected = RunWithDropEveryN(kDropEveryN, duration, kFps);

  const std::uint64_t expected_total =
      static_cast<std::uint64_t>((duration.count() * static_cast<std::int64_t>(kFps)) / 1000);
  const std::uint64_t expected_dropped = expected_total / static_cast<std::uint64_t>(kDropEveryN);
  const std::uint64_t expected_received = expected_total - expected_dropped;
  const double expected_drop_rate_percent =
      (static_cast<double>(expected_dropped) * 100.0) / static_cast<double>(expected_total);

  if (baseline.dropped_frames_total != 0U || baseline.drop_rate_percent != 0.0) {
    Fail("baseline drop metrics should be zero when drop_every_n is disabled");
  }
  if (baseline.dropped_generic_frames_total != 0U || baseline.timeout_frames_total != 0U ||
      baseline.incomplete_frames_total != 0U) {
    Fail("baseline category metrics should be zero when drop injection is disabled");
  }

  if (injected.frames_total != expected_total) {
    Fail("injected total frames mismatch");
  }
  if (injected.dropped_frames_total != expected_dropped) {
    Fail("injected dropped frames mismatch");
  }
  if (injected.dropped_generic_frames_total != expected_dropped) {
    Fail("sim drop injection should map to generic dropped category");
  }
  if (injected.timeout_frames_total != 0U || injected.incomplete_frames_total != 0U) {
    Fail("sim drop injection should not populate timeout/incomplete categories");
  }
  if (injected.received_frames_total != expected_received) {
    Fail("injected received frames mismatch");
  }
  AssertNear(injected.drop_rate_percent, expected_drop_rate_percent, 1e-9,
             "injected drop rate percent mismatch");

  if (injected.dropped_frames_total <= baseline.dropped_frames_total) {
    Fail("injected drop config should increase dropped frame count");
  }

  const fs::path out_dir = fs::temp_directory_path() / "labops-drop-injection-smoke";
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  std::string error;
  fs::path written_path;
  if (!labops::artifacts::WriteMetricsCsv(injected, out_dir, written_path, error)) {
    Fail("WriteMetricsCsv failed: " + error);
  }

  std::ifstream in(written_path, std::ios::binary);
  if (!in) {
    Fail("failed to open metrics.csv");
  }
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  AssertContains(content, "drops_total,,,80,16");
  AssertContains(content, "drop_rate_percent,,,80,20.000000");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "drop_injection_smoke: ok\n";
  return 0;
}
