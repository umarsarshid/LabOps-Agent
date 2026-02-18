#pragma once

#include "backends/camera_backend.hpp"
#include "backends/real_sdk/frame_provider.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace labops::backends::real_sdk {

// Event classification derived from frame outcomes.
//
// These are not persisted directly; they provide a stable test contract for
// validating acquisition loop behavior before router/event-writer integration.
enum class AcquisitionEventType {
  kFrameReceived = 0,
  kFrameDropped,
  kFrameTimeout,
  kFrameIncomplete,
};

struct AcquisitionLoopInput {
  std::chrono::milliseconds duration = std::chrono::milliseconds::zero();
  double frame_rate_fps = 0.0;
  std::uint32_t default_frame_size_bytes = 0U;
  std::uint64_t first_frame_id = 0U;
  std::chrono::system_clock::time_point stream_start_ts{};
};

struct AcquisitionLoopCounters {
  std::uint64_t frames_total = 0U;
  std::uint64_t frames_received = 0U;
  std::uint64_t frames_dropped = 0U;
  std::uint64_t frames_timeout = 0U;
  std::uint64_t frames_incomplete = 0U;

  // Sum of synthetic stall periods applied by the provider.
  std::uint64_t stall_periods_total = 0U;
};

struct AcquisitionLoopResult {
  std::vector<FrameSample> frames;
  std::vector<AcquisitionEventType> events;
  AcquisitionLoopCounters counters;
  std::uint64_t next_frame_id = 0U;
};

AcquisitionEventType ToAcquisitionEventType(const FrameSample& frame);

bool RunAcquisitionLoop(IFrameProvider& provider, const AcquisitionLoopInput& input,
                        AcquisitionLoopResult& result, std::string& error);

} // namespace labops::backends::real_sdk
