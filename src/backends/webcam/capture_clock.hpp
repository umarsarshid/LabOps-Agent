#pragma once

#include <chrono>

namespace labops::backends::webcam {

// CaptureClock bridges monotonic capture timing to contract-safe wall timestamps.
//
// Why this exists:
// - capture/session internals should use `steady_clock` to avoid wall-clock
//   jumps (NTP/user clock changes) affecting frame cadence logic.
// - event/metrics/report contracts currently use `system_clock` timestamps.
//
// This class keeps one anchor pair and maps steady timestamps to wall time by
// adding the steady delta onto the wall anchor.
class CaptureClock {
public:
  using SteadyTimePoint = std::chrono::steady_clock::time_point;
  using WallTimePoint = std::chrono::system_clock::time_point;

  CaptureClock();

  // Resets anchor pair to current time.
  void ResetToNow();

  // Creates a clock with explicit anchors (useful for deterministic tests).
  static CaptureClock Anchored(WallTimePoint wall_anchor, SteadyTimePoint steady_anchor);

  // Converts a steady timestamp into wall-clock contract time.
  WallTimePoint ToWallTime(SteadyTimePoint steady_ts) const;

  // Convenience helpers used by capture loops.
  SteadyTimePoint NowSteadyTime() const;
  WallTimePoint NowWallTime() const;

private:
  CaptureClock(WallTimePoint wall_anchor, SteadyTimePoint steady_anchor);

  WallTimePoint wall_anchor_{};
  SteadyTimePoint steady_anchor_{};
};

} // namespace labops::backends::webcam
