#include "backends/webcam/linux/platform_probe_linux.hpp"

namespace labops::backends::webcam {

PlatformAvailability ProbePlatformAvailabilityLinux() {
  PlatformAvailability probe;
  probe.platform_name = "linux";
  probe.unavailability_reason =
      "V4L2 capture path is not implemented yet for webcam backend";
  return probe;
}

} // namespace labops::backends::webcam
