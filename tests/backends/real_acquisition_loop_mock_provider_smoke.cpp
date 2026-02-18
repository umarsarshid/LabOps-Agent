#include "backends/real_sdk/acquisition_loop.hpp"
#include "backends/real_sdk/frame_provider.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

class MockFrameProvider final : public labops::backends::real_sdk::IFrameProvider {
public:
  explicit MockFrameProvider(std::vector<labops::backends::real_sdk::FrameProviderSample> script)
      : script_(std::move(script)) {}

  bool Next(std::uint64_t /*frame_id*/, labops::backends::real_sdk::FrameProviderSample& sample,
            std::string& error) override {
    if (next_index_ >= script_.size()) {
      error = "mock frame script exhausted";
      return false;
    }
    sample = script_[next_index_++];
    error.clear();
    return true;
  }

  std::size_t next_index() const {
    return next_index_;
  }

private:
  std::vector<labops::backends::real_sdk::FrameProviderSample> script_;
  std::size_t next_index_ = 0U;
};

std::uint64_t
CountEvents(const std::vector<labops::backends::real_sdk::AcquisitionEventType>& events,
            const labops::backends::real_sdk::AcquisitionEventType type) {
  std::uint64_t count = 0U;
  for (const auto event : events) {
    if (event == type) {
      ++count;
    }
  }
  return count;
}

void AssertStrictlyIncreasingTimestamps(const std::vector<labops::backends::FrameSample>& frames) {
  for (std::size_t i = 1; i < frames.size(); ++i) {
    if (frames[i].timestamp <= frames[i - 1].timestamp) {
      Fail("expected strictly increasing frame timestamps");
    }
  }
}

} // namespace

int main() {
  using labops::backends::FrameOutcome;
  using labops::backends::real_sdk::AcquisitionEventType;
  using labops::backends::real_sdk::AcquisitionLoopInput;
  using labops::backends::real_sdk::AcquisitionLoopResult;
  using labops::backends::real_sdk::FrameProviderSample;
  using labops::backends::real_sdk::RunAcquisitionLoop;

  // Scripted outcomes include:
  // - timeout frames
  // - incomplete frames
  // - two burst-stall injections (3 + 2 frame periods)
  const std::vector<FrameProviderSample> scripted_samples = {
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4096, .stall_periods = 0},
      {.outcome = FrameOutcome::kTimeout, .size_bytes = 0, .stall_periods = 0},
      {.outcome = FrameOutcome::kIncomplete, .size_bytes = 700, .stall_periods = 0},
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4096, .stall_periods = 0},
      {.outcome = FrameOutcome::kTimeout, .size_bytes = 0, .stall_periods = 3},
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4096, .stall_periods = 2},
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4096, .stall_periods = 0},
      {.outcome = FrameOutcome::kIncomplete, .size_bytes = 512, .stall_periods = 0},
      {.outcome = FrameOutcome::kTimeout, .size_bytes = 0, .stall_periods = 0},
      {.outcome = FrameOutcome::kReceived, .size_bytes = 4096, .stall_periods = 0},
  };

  MockFrameProvider provider(scripted_samples);
  AcquisitionLoopInput input;
  input.duration = std::chrono::milliseconds(1000); // 10 frames @ 10fps
  input.frame_rate_fps = 10.0;
  input.default_frame_size_bytes = 4096U;
  input.first_frame_id = 42U;
  input.stream_start_ts =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000LL));

  AcquisitionLoopResult result;
  std::string error;
  if (!RunAcquisitionLoop(provider, input, result, error)) {
    Fail("mock-provider acquisition loop should succeed: " + error);
  }

  if (provider.next_index() != scripted_samples.size()) {
    Fail("acquisition loop should consume all scripted provider samples");
  }
  if (result.next_frame_id != 52U) {
    Fail("unexpected next_frame_id after scripted loop");
  }
  if (result.frames.size() != scripted_samples.size()) {
    Fail("unexpected frame count from scripted loop");
  }
  if (result.events.size() != scripted_samples.size()) {
    Fail("event vector should align one-to-one with produced frames");
  }
  AssertStrictlyIncreasingTimestamps(result.frames);

  if (result.counters.frames_total != 10U) {
    Fail("unexpected frames_total counter");
  }
  if (result.counters.frames_received != 5U) {
    Fail("unexpected frames_received counter");
  }
  if (result.counters.frames_timeout != 3U) {
    Fail("unexpected frames_timeout counter");
  }
  if (result.counters.frames_incomplete != 2U) {
    Fail("unexpected frames_incomplete counter");
  }
  if (result.counters.frames_dropped != 5U) {
    Fail("unexpected frames_dropped counter");
  }
  if (result.counters.stall_periods_total != 5U) {
    Fail("unexpected stall_periods_total counter");
  }

  if (CountEvents(result.events, AcquisitionEventType::kFrameReceived) != 5U) {
    Fail("expected 5 FRAME_RECEIVED-equivalent events");
  }
  if (CountEvents(result.events, AcquisitionEventType::kFrameTimeout) != 3U) {
    Fail("expected 3 FRAME_TIMEOUT-equivalent events");
  }
  if (CountEvents(result.events, AcquisitionEventType::kFrameIncomplete) != 2U) {
    Fail("expected 2 FRAME_INCOMPLETE-equivalent events");
  }
  if (CountEvents(result.events, AcquisitionEventType::kFrameDropped) != 0U) {
    Fail("script does not include generic dropped outcomes");
  }

  if (result.frames[1].outcome != FrameOutcome::kTimeout || !result.frames[1].dropped.has_value() ||
      !result.frames[1].dropped.value() || result.frames[1].size_bytes != 0U) {
    Fail("timeout frame normalization is incorrect");
  }
  if (result.frames[2].outcome != FrameOutcome::kIncomplete ||
      !result.frames[2].dropped.has_value() || !result.frames[2].dropped.value() ||
      result.frames[2].size_bytes == 0U) {
    Fail("incomplete frame normalization is incorrect");
  }

  // Burst stalls should create visible timestamp gaps beyond nominal 100ms.
  const auto gap_34 = std::chrono::duration_cast<std::chrono::milliseconds>(
      result.frames[4].timestamp - result.frames[3].timestamp);
  const auto gap_45 = std::chrono::duration_cast<std::chrono::milliseconds>(
      result.frames[5].timestamp - result.frames[4].timestamp);
  if (gap_34.count() < 300) {
    Fail("expected burst stall to create >=300ms gap at frame[4]");
  }
  if (gap_45.count() < 200) {
    Fail("expected second burst stall to create >=200ms gap at frame[5]");
  }

  std::cout << "real_acquisition_loop_mock_provider_smoke: ok\n";
  return 0;
}
