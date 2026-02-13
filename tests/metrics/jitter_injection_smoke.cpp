#include "backends/camera_backend.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "metrics/fps.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

labops::metrics::FpsReport RunWithJitter(std::uint32_t jitter_us) {
  using labops::backends::ICameraBackend;
  using labops::backends::sim::SimCameraBackend;
  using labops::backends::sim::SimScenarioConfig;

  std::unique_ptr<ICameraBackend> backend = std::make_unique<SimCameraBackend>();
  std::string error;
  if (!backend->Connect(error)) {
    Fail("backend connect failed: " + error);
  }

  SimScenarioConfig config;
  config.fps = 60;
  config.jitter_us = jitter_us;
  config.seed = 4242;
  config.frame_size_bytes = 2048;
  config.drop_every_n = 0;
  config.faults.drop_percent = 0;
  config.faults.burst_drop = 0;
  config.faults.reorder = 0;
  if (!labops::backends::sim::ApplyScenarioConfig(*backend, config, error)) {
    Fail("scenario apply failed: " + error);
  }

  if (!backend->Start(error)) {
    Fail("backend start failed: " + error);
  }

  const auto duration = std::chrono::milliseconds(3000);
  const std::vector<labops::backends::FrameSample> frames = backend->PullFrames(duration, error);
  if (!error.empty()) {
    Fail("pull_frames failed: " + error);
  }

  if (!backend->Stop(error)) {
    Fail("backend stop failed: " + error);
  }

  labops::metrics::FpsReport report;
  if (!labops::metrics::ComputeFpsReport(frames,
                                         duration,
                                         std::chrono::milliseconds(1000),
                                         report,
                                         error)) {
    Fail("ComputeFpsReport failed: " + error);
  }

  return report;
}

} // namespace

int main() {
  const auto low_jitter = RunWithJitter(0);
  const auto high_jitter = RunWithJitter(7000);

  if (low_jitter.inter_frame_interval_us.sample_count == 0U ||
      high_jitter.inter_frame_interval_us.sample_count == 0U) {
    Fail("inter-frame stats should have non-zero samples");
  }

  // Main milestone assertion: injected jitter should be visible in computed
  // jitter/timing metrics.
  if (high_jitter.inter_frame_jitter_us.avg_us <= low_jitter.inter_frame_jitter_us.avg_us + 500.0) {
    std::cerr << "low jitter avg_us: " << low_jitter.inter_frame_jitter_us.avg_us << '\n';
    std::cerr << "high jitter avg_us: " << high_jitter.inter_frame_jitter_us.avg_us << '\n';
    Fail("expected higher jitter scenario to raise avg inter-frame jitter");
  }

  if (high_jitter.inter_frame_interval_us.p95_us <= low_jitter.inter_frame_interval_us.p95_us + 500.0) {
    std::cerr << "low jitter p95_us: " << low_jitter.inter_frame_interval_us.p95_us << '\n';
    std::cerr << "high jitter p95_us: " << high_jitter.inter_frame_interval_us.p95_us << '\n';
    Fail("expected higher jitter scenario to raise p95 inter-frame interval");
  }

  std::cout << "jitter_injection_smoke: ok\n";
  return 0;
}
