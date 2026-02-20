#include "backends/webcam/linux/platform_probe_linux.hpp"

#include "backends/webcam/opencv_bootstrap.hpp"

namespace labops::backends::webcam {

PlatformAvailability ProbePlatformAvailabilityLinux() {
  PlatformAvailability probe;
  probe.platform_name = "linux";
  if (IsOpenCvBootstrapEnabled()) {
    probe.available = true;
    probe.unavailability_reason = "enabled";
    probe.capabilities.pixel_format = CapabilityState::kBestEffort;
    probe.capabilities.frame_rate = CapabilityState::kBestEffort;
    return probe;
  }

  probe.unavailability_reason =
      "OpenCV webcam bootstrap is not compiled (set LABOPS_ENABLE_WEBCAM_OPENCV=ON)";
  return probe;
}

} // namespace labops::backends::webcam
