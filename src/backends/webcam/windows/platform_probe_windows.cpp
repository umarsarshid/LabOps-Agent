#include "backends/webcam/windows/platform_probe_windows.hpp"

namespace labops::backends::webcam {

PlatformAvailability ProbePlatformAvailabilityWindows() {
  PlatformAvailability probe;
  probe.platform_name = "windows";
  probe.unavailability_reason =
      "Media Foundation webcam capture path is not implemented yet";
  return probe;
}

} // namespace labops::backends::webcam
