#include "backends/camera_backend.hpp"
#include "backends/real_sdk/real_backend.hpp"
#include "metrics/fps.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertMonotonicTimestamps(const std::vector<labops::backends::FrameSample>& frames) {
  for (std::size_t i = 1; i < frames.size(); ++i) {
    if (frames[i].timestamp <= frames[i - 1].timestamp) {
      Fail("expected strictly increasing frame timestamps");
    }
  }
}

void AssertRange(double value, double min_inclusive, double max_inclusive, std::string_view label) {
  if (value < min_inclusive || value > max_inclusive) {
    std::cerr << "expected " << label << " in [" << min_inclusive << ", " << max_inclusive
              << "], got " << value << '\n';
    std::abort();
  }
}

double RunMeasuredAvgFps(std::uint32_t requested_fps) {
  using labops::backends::real_sdk::RealBackend;

  RealBackend backend;
  std::string error;
  if (!backend.Connect(error)) {
    Fail("expected connect to succeed for frame-rate control check");
  }
  if (!backend.SetParam("AcquisitionFrameRate", std::to_string(requested_fps), error)) {
    Fail("expected AcquisitionFrameRate set to succeed");
  }
  if (!backend.SetParam("FrameTimeoutPercent", "0", error) ||
      !backend.SetParam("FrameIncompletePercent", "0", error)) {
    Fail("expected zero drop outcomes for frame-rate control check");
  }
  if (!backend.Start(error)) {
    Fail("expected start to succeed for frame-rate control check");
  }

  const std::chrono::milliseconds duration(8'000);
  const std::vector<labops::backends::FrameSample> frames = backend.PullFrames(duration, error);
  if (!error.empty() || frames.empty()) {
    Fail("expected non-empty frames for frame-rate control check");
  }

  labops::metrics::FpsReport report;
  if (!labops::metrics::ComputeFpsReport(frames, duration, std::chrono::milliseconds(1'000), report,
                                         error)) {
    Fail("expected fps report for frame-rate control check");
  }
  if (!backend.Stop(error)) {
    Fail("expected stop to succeed for frame-rate control check");
  }

  return report.avg_fps;
}

} // namespace

int main() {
  using labops::backends::FrameOutcome;
  using labops::backends::real_sdk::RealBackend;

  RealBackend backend;
  std::string error;
  if (!backend.Connect(error)) {
    Fail("expected real backend connect to succeed");
  }
  if (!backend.SetParam("AcquisitionFrameRate", "25", error)) {
    Fail("expected setting AcquisitionFrameRate to succeed");
  }
  if (!backend.SetParam("FrameTimeoutPercent", "12", error)) {
    Fail("expected setting FrameTimeoutPercent to succeed");
  }
  if (!backend.SetParam("FrameIncompletePercent", "8", error)) {
    Fail("expected setting FrameIncompletePercent to succeed");
  }
  if (!backend.SetParam("FrameSeed", "777", error)) {
    Fail("expected setting FrameSeed to succeed");
  }
  if (!backend.Start(error)) {
    Fail("expected real backend start to succeed");
  }

  const std::chrono::milliseconds duration(10'000);
  const std::vector<labops::backends::FrameSample> frames = backend.PullFrames(duration, error);
  if (!error.empty()) {
    Fail("expected real frame acquisition loop to succeed");
  }
  if (frames.empty()) {
    Fail("expected non-empty frame set for 10s pull");
  }
  if (frames.size() != 250U) {
    Fail("expected 250 frames for 10s at 25fps");
  }
  AssertMonotonicTimestamps(frames);

  std::uint64_t received = 0;
  std::uint64_t timeout = 0;
  std::uint64_t incomplete = 0;
  for (const auto& frame : frames) {
    switch (frame.outcome) {
    case FrameOutcome::kReceived:
      ++received;
      if (frame.dropped.has_value() && frame.dropped.value()) {
        Fail("received frame should not be marked dropped");
      }
      break;
    case FrameOutcome::kTimeout:
      ++timeout;
      if (!frame.dropped.has_value() || !frame.dropped.value()) {
        Fail("timeout frame must be marked dropped");
      }
      break;
    case FrameOutcome::kIncomplete:
      ++incomplete;
      if (!frame.dropped.has_value() || !frame.dropped.value()) {
        Fail("incomplete frame must be marked dropped");
      }
      if (frame.size_bytes == 0U) {
        Fail("incomplete frame should retain partial payload bytes");
      }
      break;
    case FrameOutcome::kDropped:
      Fail("real acquisition smoke should not emit generic dropped outcome");
      break;
    }
  }

  if (received == 0U || timeout == 0U || incomplete == 0U) {
    Fail("expected received, timeout, and incomplete outcomes in acquired frames");
  }

  labops::metrics::FpsReport report;
  if (!labops::metrics::ComputeFpsReport(frames, duration, std::chrono::milliseconds(1'000), report,
                                         error)) {
    Fail("expected fps metrics computation to succeed for real frame samples");
  }
  if (report.frames_total != frames.size()) {
    Fail("fps report total frame count mismatch");
  }
  if (report.timeout_frames_total != timeout) {
    Fail("fps report timeout count mismatch");
  }
  if (report.incomplete_frames_total != incomplete) {
    Fail("fps report incomplete count mismatch");
  }
  if (report.dropped_generic_frames_total != 0U) {
    Fail("real acquisition smoke should not add generic drop category");
  }
  if (report.dropped_frames_total != timeout + incomplete) {
    Fail("fps report dropped total should equal timeout + incomplete");
  }
  if (report.avg_fps <= 0.0) {
    Fail("expected positive avg fps from received frame set");
  }

  if (!backend.Stop(error)) {
    Fail("expected real backend stop to succeed");
  }

  // Frame-rate control should be visible in measured FPS when supported.
  const double low_fps_measured = RunMeasuredAvgFps(12U);
  const double high_fps_measured = RunMeasuredAvgFps(48U);
  AssertRange(low_fps_measured, 11.0, 13.0, "low_fps_measured");
  AssertRange(high_fps_measured, 47.0, 49.5, "high_fps_measured");
  if (high_fps_measured < low_fps_measured + 30.0) {
    Fail("expected measurable FPS increase after frame-rate change");
  }

  std::cout << "real_frame_acquisition_smoke: ok\n";
  return 0;
}
