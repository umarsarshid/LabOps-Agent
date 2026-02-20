#include "backends/webcam/capture_clock.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

} // namespace

int main() {
  using labops::backends::webcam::CaptureClock;

  // Deterministic anchor mapping check: wall delta must match steady delta.
  const auto wall_anchor =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000LL));
  const auto steady_anchor =
      std::chrono::steady_clock::time_point(std::chrono::milliseconds(5'000));
  const CaptureClock anchored = CaptureClock::Anchored(wall_anchor, steady_anchor);

  const auto mapped_same = anchored.ToWallTime(steady_anchor);
  if (mapped_same != wall_anchor) {
    Fail("anchored capture clock did not preserve anchor equivalence");
  }

  const auto mapped_plus = anchored.ToWallTime(steady_anchor + std::chrono::milliseconds(250));
  const auto plus_delta =
      std::chrono::duration_cast<std::chrono::milliseconds>(mapped_plus - wall_anchor);
  if (plus_delta.count() != 250LL) {
    Fail("anchored capture clock produced unexpected positive delta");
  }

  const auto mapped_minus = anchored.ToWallTime(steady_anchor - std::chrono::milliseconds(120));
  const auto minus_delta =
      std::chrono::duration_cast<std::chrono::milliseconds>(mapped_minus - wall_anchor);
  if (minus_delta.count() != -120LL) {
    Fail("anchored capture clock produced unexpected negative delta");
  }

  // Live monotonic check: converting increasing steady points must not move
  // backwards in wall-time representation.
  CaptureClock live;
  const auto steady_1 = live.NowSteadyTime();
  const auto wall_1 = live.ToWallTime(steady_1);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  const auto steady_2 = live.NowSteadyTime();
  const auto wall_2 = live.ToWallTime(steady_2);
  if (steady_2 < steady_1) {
    Fail("steady clock moved backwards unexpectedly");
  }
  if (wall_2 < wall_1) {
    Fail("capture clock conversion moved wall timestamp backwards");
  }

  const auto wall_now = live.NowWallTime();
  if (wall_now < wall_2) {
    Fail("now_wall_time should not be earlier than previous mapped wall time");
  }

  std::cout << "webcam_capture_clock_smoke: ok\n";
  return 0;
}
