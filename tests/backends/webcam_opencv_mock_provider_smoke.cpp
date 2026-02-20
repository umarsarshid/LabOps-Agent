#include "backends/webcam/opencv_webcam_impl.hpp"
#include "backends/webcam/testing/mock_frame_provider.hpp"
#include "metrics/fps.hpp"

#include <chrono>
#include <cstdint>
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

std::uint64_t CountOutcome(const std::vector<labops::backends::FrameSample>& frames,
                           const labops::backends::FrameOutcome outcome) {
  std::uint64_t count = 0U;
  for (const auto& frame : frames) {
    if (frame.outcome == outcome) {
      ++count;
    }
  }
  return count;
}

} // namespace

int main() {
  using labops::backends::FrameOutcome;
  using labops::backends::FrameSample;
  using labops::backends::webcam::OpenCvCaptureProperty;
  using labops::backends::webcam::OpenCvWebcamImpl;
  using labops::backends::webcam::WebcamFrameProviderSample;
  using labops::backends::webcam::testing::MockFrameProvider;

  OpenCvWebcamImpl impl;

  const std::vector<WebcamFrameProviderSample> script = {
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4000U, .stall_periods = 0U},
      {.outcome = FrameOutcome::kTimeout, .size_bytes = 0U, .stall_periods = 0U},
      {.outcome = FrameOutcome::kIncomplete, .size_bytes = 700U, .stall_periods = 0U},
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4096U, .stall_periods = 3U},
      {.outcome = FrameOutcome::kReceived, .size_bytes = 2048U, .stall_periods = 0U},
      {.outcome = FrameOutcome::kTimeout, .size_bytes = 0U, .stall_periods = 0U},
  };
  auto provider = std::make_unique<MockFrameProvider>(script);
  MockFrameProvider* provider_ptr = provider.get();
  const auto start_ts =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000LL));
  impl.EnableTestMode(std::move(provider), std::chrono::milliseconds(100), start_ts);
  if (!impl.IsTestModeEnabled()) {
    Fail("test mode should be enabled");
  }

  std::string error;
  if (!impl.OpenDevice(0U, error)) {
    Fail("test mode open_device should succeed: " + error);
  }

  if (!impl.SetProperty(OpenCvCaptureProperty::kFrameWidth, 1280.0, error) ||
      !impl.SetProperty(OpenCvCaptureProperty::kFrameHeight, 720.0, error) ||
      !impl.SetProperty(OpenCvCaptureProperty::kFps, 10.0, error)) {
    Fail("test mode set property should succeed: " + error);
  }
  if (!impl.SetFourcc("YUY2", error)) {
    Fail("test mode set fourcc should succeed: " + error);
  }

  double read_back = 0.0;
  if (!impl.GetProperty(OpenCvCaptureProperty::kFps, read_back, error) || read_back != 10.0) {
    Fail("test mode fps readback should be 10.0");
  }
  std::string fourcc;
  if (!impl.GetFourcc(fourcc, error) || fourcc != "YUY2") {
    Fail("test mode fourcc readback should be YUY2");
  }

  std::uint64_t next_frame_id = 50U;
  const std::vector<FrameSample> frames =
      impl.PullFrames(std::chrono::milliseconds(600), next_frame_id, error);
  if (!error.empty()) {
    Fail("test mode pull_frames should succeed: " + error);
  }
  if (provider_ptr->next_index() != provider_ptr->script_size()) {
    Fail("mock provider should consume all scripted samples");
  }
  if (frames.size() != script.size()) {
    Fail("pull_frames should emit one frame per script sample");
  }
  if (next_frame_id != 56U) {
    Fail("unexpected next_frame_id after scripted pull");
  }

  if (CountOutcome(frames, FrameOutcome::kReceived) != 3U) {
    Fail("expected exactly 3 received frames");
  }
  if (CountOutcome(frames, FrameOutcome::kTimeout) != 2U) {
    Fail("expected exactly 2 timeout frames");
  }
  if (CountOutcome(frames, FrameOutcome::kIncomplete) != 1U) {
    Fail("expected exactly 1 incomplete frame");
  }

  if (!frames[1].dropped.has_value() || !frames[1].dropped.value() || frames[1].size_bytes != 0U) {
    Fail("timeout frame should be dropped with size 0");
  }
  if (!frames[2].dropped.has_value() || !frames[2].dropped.value() || frames[2].size_bytes == 0U) {
    Fail("incomplete frame should be dropped with non-zero size");
  }
  if (!frames[0].dropped.has_value() || frames[0].dropped.value()) {
    Fail("received frame should not be marked dropped");
  }

  const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(frames[3].timestamp -
                                                                         frames[2].timestamp);
  if (gap.count() < 400LL) {
    Fail("stall periods should create a deterministic >=400ms timestamp gap");
  }

  // Timeout/incomplete counters should roll into metrics with the same
  // category semantics used by real-backend acquisition.
  labops::metrics::FpsReport report;
  if (!labops::metrics::ComputeFpsReport(frames, std::chrono::milliseconds(600),
                                         std::chrono::milliseconds(200), report, error)) {
    Fail("ComputeFpsReport should succeed for webcam scripted frames: " + error);
  }
  if (report.frames_total != 6U) {
    Fail("metrics total frame count mismatch");
  }
  if (report.received_frames_total != 3U) {
    Fail("metrics received frame count mismatch");
  }
  if (report.timeout_frames_total != 2U) {
    Fail("metrics timeout frame count mismatch");
  }
  if (report.incomplete_frames_total != 1U) {
    Fail("metrics incomplete frame count mismatch");
  }
  if (report.dropped_generic_frames_total != 0U) {
    Fail("metrics generic drop count should remain zero for timeout/incomplete outcomes");
  }
  if (report.dropped_frames_total != report.timeout_frames_total + report.incomplete_frames_total) {
    Fail("metrics dropped total should equal timeout + incomplete counts");
  }

  if (!impl.CloseDevice(error)) {
    Fail("test mode close should succeed: " + error);
  }
  impl.DisableTestMode();
  if (impl.IsTestModeEnabled()) {
    Fail("test mode should be disabled");
  }

  std::cout << "webcam_opencv_mock_provider_smoke: ok\n";
  return 0;
}
