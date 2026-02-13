#include "backends/camera_backend.hpp"
#include "backends/sim/sim_camera_backend.hpp"

#include <chrono>
#include <cmath>
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

void SetParamOrFail(labops::backends::ICameraBackend& backend, const std::string& key,
                    const std::string& value) {
  std::string error;
  if (!backend.SetParam(key, value, error)) {
    Fail("set_param failed for '" + key + "': " + error);
  }
}

std::vector<labops::backends::FrameSample> GenerateFrames(std::uint32_t fps,
                                                          std::uint32_t jitter_us,
                                                          std::uint64_t seed,
                                                          std::uint32_t drop_every_n) {
  std::unique_ptr<labops::backends::ICameraBackend> backend =
      std::make_unique<labops::backends::sim::SimCameraBackend>();

  std::string error;
  if (!backend->Connect(error)) {
    Fail("connect failed: " + error);
  }

  SetParamOrFail(*backend, "fps", std::to_string(fps));
  SetParamOrFail(*backend, "jitter_us", std::to_string(jitter_us));
  SetParamOrFail(*backend, "seed", std::to_string(seed));
  SetParamOrFail(*backend, "frame_size_bytes", "4096");
  SetParamOrFail(*backend, "drop_every_n", std::to_string(drop_every_n));

  if (!backend->Start(error)) {
    Fail("start failed: " + error);
  }

  // At 40 FPS and 250 ms this yields 10 frames.
  const std::vector<labops::backends::FrameSample> frames =
      backend->PullFrames(std::chrono::milliseconds(250), error);
  if (!error.empty()) {
    Fail("pull_frames failed: " + error);
  }

  if (!backend->Stop(error)) {
    Fail("stop failed: " + error);
  }

  return frames;
}

std::vector<std::int64_t>
RelativeOffsetsUs(const std::vector<labops::backends::FrameSample>& frames) {
  std::vector<std::int64_t> offsets;
  offsets.reserve(frames.size());
  if (frames.empty()) {
    return offsets;
  }

  const auto first_ts = frames.front().timestamp;
  for (const auto& frame : frames) {
    const auto delta =
        std::chrono::duration_cast<std::chrono::microseconds>(frame.timestamp - first_ts);
    offsets.push_back(delta.count());
  }
  return offsets;
}

} // namespace

int main() {
  const std::uint32_t fps = 40;
  const std::uint32_t jitter_us = 1'500;

  const auto frames = GenerateFrames(fps, jitter_us, /*seed=*/123, /*drop_every_n=*/0);
  if (frames.size() != 10U) {
    Fail("expected 10 frames at 40 FPS for 250 ms duration");
  }

  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto& frame = frames[i];
    if (frame.frame_id != i) {
      Fail("frame_id sequence mismatch");
    }
    if (frame.size_bytes != 4096U) {
      Fail("unexpected frame size");
    }
    if (frame.dropped.has_value()) {
      Fail("dropped should be absent when drop_every_n=0");
    }
  }

  // Approximate timing check: window should be close to N/FPS seconds.
  const auto period_us = static_cast<std::int64_t>(1'000'000 / fps);
  const auto offsets = RelativeOffsetsUs(frames);
  const std::int64_t produced_window_us = offsets.back() + period_us;
  const std::int64_t expected_window_us = static_cast<std::int64_t>(frames.size()) * period_us;
  const std::int64_t tolerance_us = static_cast<std::int64_t>(jitter_us) * 2 + 5'000;
  if (std::llabs(produced_window_us - expected_window_us) > tolerance_us) {
    Fail("frame timing window is outside expected N/FPS envelope");
  }

  const auto same_seed_frames_a = GenerateFrames(fps, jitter_us, /*seed=*/555, /*drop_every_n=*/0);
  const auto same_seed_frames_b = GenerateFrames(fps, jitter_us, /*seed=*/555, /*drop_every_n=*/0);
  if (same_seed_frames_a.size() != same_seed_frames_b.size()) {
    Fail("same-seed frame count mismatch");
  }

  const auto offsets_a = RelativeOffsetsUs(same_seed_frames_a);
  const auto offsets_b = RelativeOffsetsUs(same_seed_frames_b);
  if (offsets_a != offsets_b) {
    Fail("same-seed jitter pattern should be deterministic");
  }

  const auto different_seed_frames =
      GenerateFrames(fps, jitter_us, /*seed=*/777, /*drop_every_n=*/0);
  const auto offsets_c = RelativeOffsetsUs(different_seed_frames);
  if (offsets_a == offsets_c) {
    Fail("different seed should produce a different jitter pattern");
  }

  const auto dropped_frames = GenerateFrames(fps, /*jitter_us=*/0, /*seed=*/9, /*drop_every_n=*/4);
  for (std::size_t i = 0; i < dropped_frames.size(); ++i) {
    const bool should_drop = ((i + 1U) % 4U) == 0U;
    if (should_drop) {
      if (!dropped_frames[i].dropped.has_value() || !dropped_frames[i].dropped.value()) {
        Fail("expected dropped=true for configured drop slot");
      }
      if (dropped_frames[i].size_bytes != 0U) {
        Fail("dropped frame should have size 0");
      }
    } else {
      if (dropped_frames[i].dropped.has_value()) {
        Fail("non-dropped frame should omit dropped flag");
      }
      if (dropped_frames[i].size_bytes != 4096U) {
        Fail("non-dropped frame should preserve configured size");
      }
    }
  }

  std::cout << "sim_frame_generator_smoke: ok\n";
  return 0;
}
