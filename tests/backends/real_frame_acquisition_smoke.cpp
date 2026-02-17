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

  std::cout << "real_frame_acquisition_smoke: ok\n";
  return 0;
}
