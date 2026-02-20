#include "backends/webcam/capture_clock.hpp"

namespace labops::backends::webcam {

CaptureClock::CaptureClock() {
  ResetToNow();
}

void CaptureClock::ResetToNow() {
  steady_anchor_ = std::chrono::steady_clock::now();
  wall_anchor_ = std::chrono::system_clock::now();
}

CaptureClock CaptureClock::Anchored(const WallTimePoint wall_anchor,
                                    const SteadyTimePoint steady_anchor) {
  return CaptureClock(wall_anchor, steady_anchor);
}

CaptureClock::WallTimePoint CaptureClock::ToWallTime(const SteadyTimePoint steady_ts) const {
  const auto steady_delta = steady_ts - steady_anchor_;
  return wall_anchor_ + std::chrono::duration_cast<WallTimePoint::duration>(steady_delta);
}

CaptureClock::SteadyTimePoint CaptureClock::NowSteadyTime() const {
  return std::chrono::steady_clock::now();
}

CaptureClock::WallTimePoint CaptureClock::NowWallTime() const {
  return ToWallTime(NowSteadyTime());
}

CaptureClock::CaptureClock(const WallTimePoint wall_anchor, const SteadyTimePoint steady_anchor)
    : wall_anchor_(wall_anchor), steady_anchor_(steady_anchor) {}

} // namespace labops::backends::webcam
