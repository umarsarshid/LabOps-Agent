#include "backends/real_sdk/acquisition_loop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace labops::backends::real_sdk {

AcquisitionEventType ToAcquisitionEventType(const FrameSample& frame) {
  switch (frame.outcome) {
  case FrameOutcome::kTimeout:
    return AcquisitionEventType::kFrameTimeout;
  case FrameOutcome::kIncomplete:
    return AcquisitionEventType::kFrameIncomplete;
  case FrameOutcome::kDropped:
    return AcquisitionEventType::kFrameDropped;
  case FrameOutcome::kReceived:
  default:
    if (frame.dropped.has_value() && frame.dropped.value()) {
      return AcquisitionEventType::kFrameDropped;
    }
    return AcquisitionEventType::kFrameReceived;
  }
}

bool RunAcquisitionLoop(IFrameProvider& provider, const AcquisitionLoopInput& input,
                        AcquisitionLoopResult& result, std::string& error) {
  result = AcquisitionLoopResult{};
  error.clear();

  if (input.duration < std::chrono::milliseconds::zero()) {
    error = "acquisition loop duration cannot be negative";
    return false;
  }
  if (!std::isfinite(input.frame_rate_fps) || input.frame_rate_fps <= 0.0) {
    error = "acquisition loop requires a positive finite frame_rate_fps";
    return false;
  }
  if (input.default_frame_size_bytes == 0U) {
    error = "acquisition loop requires default_frame_size_bytes > 0";
    return false;
  }
  if (input.duration == std::chrono::milliseconds::zero()) {
    result.next_frame_id = input.first_frame_id;
    return true;
  }

  const double frame_count_exact =
      (static_cast<double>(input.duration.count()) * input.frame_rate_fps) / 1000.0;
  const std::uint64_t frame_count =
      frame_count_exact > 0.0 ? static_cast<std::uint64_t>(frame_count_exact) : 0U;
  if (frame_count == 0U) {
    result.next_frame_id = input.first_frame_id;
    return true;
  }

  const double period_ns_double = 1'000'000'000.0 / input.frame_rate_fps;
  const auto period_ns_count = static_cast<std::int64_t>(std::llround(period_ns_double));
  const auto frame_period_ns = std::chrono::nanoseconds(std::max<std::int64_t>(1, period_ns_count));

  result.frames.reserve(static_cast<std::size_t>(frame_count));
  result.events.reserve(static_cast<std::size_t>(frame_count));

  std::uint64_t stall_periods_total = 0U;
  for (std::uint64_t index = 0; index < frame_count; ++index) {
    const std::uint64_t frame_id = input.first_frame_id + index;

    FrameProviderSample provided;
    if (!provider.Next(frame_id, provided, error)) {
      return false;
    }

    stall_periods_total += static_cast<std::uint64_t>(provided.stall_periods);
    const std::uint64_t logical_period_index = frame_id + stall_periods_total;

    FrameSample frame;
    frame.frame_id = frame_id;
    frame.outcome = provided.outcome;
    frame.timestamp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        input.stream_start_ts + frame_period_ns * static_cast<std::int64_t>(logical_period_index));
    if (!result.frames.empty() && frame.timestamp <= result.frames.back().timestamp) {
      frame.timestamp = result.frames.back().timestamp + std::chrono::microseconds(1);
    }

    switch (provided.outcome) {
    case FrameOutcome::kTimeout:
      frame.size_bytes = 0U;
      frame.dropped = true;
      ++result.counters.frames_timeout;
      ++result.counters.frames_dropped;
      break;
    case FrameOutcome::kIncomplete:
      frame.size_bytes = provided.size_bytes == 0U
                             ? std::max<std::uint32_t>(1U, input.default_frame_size_bytes / 4U)
                             : provided.size_bytes;
      frame.dropped = true;
      ++result.counters.frames_incomplete;
      ++result.counters.frames_dropped;
      break;
    case FrameOutcome::kDropped:
      frame.size_bytes = 0U;
      frame.dropped = true;
      ++result.counters.frames_dropped;
      break;
    case FrameOutcome::kReceived:
    default:
      frame.size_bytes =
          provided.size_bytes == 0U ? input.default_frame_size_bytes : provided.size_bytes;
      ++result.counters.frames_received;
      break;
    }

    result.events.push_back(ToAcquisitionEventType(frame));
    result.frames.push_back(frame);
  }

  result.counters.frames_total = static_cast<std::uint64_t>(result.frames.size());
  result.counters.stall_periods_total = stall_periods_total;
  result.next_frame_id = input.first_frame_id + frame_count;
  return true;
}

} // namespace labops::backends::real_sdk
