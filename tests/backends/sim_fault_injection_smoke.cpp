#include "backends/camera_backend.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

std::vector<labops::backends::FrameSample>
RunScenario(const labops::backends::sim::SimScenarioConfig& config) {
  std::unique_ptr<labops::backends::ICameraBackend> backend =
      std::make_unique<labops::backends::sim::SimCameraBackend>();

  std::string error;
  if (!backend->Connect(error)) {
    Fail("connect failed: " + error);
  }

  if (!labops::backends::sim::ApplyScenarioConfig(*backend, config, error)) {
    Fail("apply scenario config failed: " + error);
  }

  if (!backend->Start(error)) {
    Fail("start failed: " + error);
  }

  // At 30 FPS and 500 ms => 15 frames.
  const auto frames = backend->PullFrames(std::chrono::milliseconds(500), error);
  if (!error.empty()) {
    Fail("pull_frames failed: " + error);
  }

  if (!backend->Stop(error)) {
    Fail("stop failed: " + error);
  }

  return frames;
}

std::vector<std::uint64_t>
ExtractDropPattern(const std::vector<labops::backends::FrameSample>& frames) {
  std::vector<std::uint64_t> dropped_frame_ids;
  for (const auto& frame : frames) {
    if (frame.dropped.has_value() && frame.dropped.value()) {
      dropped_frame_ids.push_back(frame.frame_id);
    }
  }
  return dropped_frame_ids;
}

std::vector<std::uint64_t> ExtractOrder(const std::vector<labops::backends::FrameSample>& frames) {
  std::vector<std::uint64_t> order;
  order.reserve(frames.size());
  for (const auto& frame : frames) {
    order.push_back(frame.frame_id);
  }
  return order;
}

} // namespace

int main() {
  labops::backends::sim::SimScenarioConfig config;
  config.fps = 30;
  config.jitter_us = 500;
  config.seed = 1234;
  config.frame_size_bytes = 2048;
  config.drop_every_n = 0;
  config.faults.drop_percent = 25;
  config.faults.burst_drop = 2;
  config.faults.reorder = 4;

  const auto run_a = RunScenario(config);
  const auto run_b = RunScenario(config);
  if (run_a.size() != run_b.size()) {
    Fail("same-seed run sizes mismatch");
  }

  // Known-good deterministic drop pattern for this scenario/seed.
  const std::vector<std::uint64_t> expected_drop_ids = {6, 5, 10, 9, 12, 13, 14};
  const auto drops_a = ExtractDropPattern(run_a);
  const auto drops_b = ExtractDropPattern(run_b);
  if (drops_a != expected_drop_ids) {
    Fail("drop pattern does not match expected known pattern");
  }
  if (drops_b != expected_drop_ids) {
    Fail("second run does not reproduce known drop pattern");
  }

  // Reorder should also be deterministic with the same seed/config.
  const auto order_a = ExtractOrder(run_a);
  const auto order_b = ExtractOrder(run_b);
  if (order_a != order_b) {
    Fail("same-seed reorder output mismatch");
  }

  labops::backends::sim::SimScenarioConfig different_seed = config;
  different_seed.seed = 9999;
  const auto run_c = RunScenario(different_seed);
  const auto drops_c = ExtractDropPattern(run_c);
  if (drops_c == drops_a) {
    Fail("different seed should produce a different drop pattern");
  }

  std::cout << "sim_fault_injection_smoke: ok\n";
  return 0;
}
