#include "backends/webcam/macos/platform_probe_macos.hpp"

namespace labops::backends::webcam {

PlatformAvailability ProbePlatformAvailabilityMacos() {
  PlatformAvailability probe;
  probe.platform_name = "macos";
  probe.unavailability_reason =
      "AVFoundation webcam capture path is not implemented yet";
  return probe;
}

} // namespace labops::backends::webcam
