#include "backends/webcam/linux/platform_probe_linux.hpp"

#include "backends/webcam/opencv_bootstrap.hpp"

namespace labops::backends::webcam {

PlatformAvailability ProbePlatformAvailabilityLinux() {
  PlatformAvailability probe;
  probe.platform_name = "linux";
  // Linux native V4L2 path is compiled independently from OpenCV.
  // OpenCV is only a fallback path when native open/stream setup cannot be
  // used for a selected device.
  probe.available = true;
  probe.unavailability_reason = IsOpenCvBootstrapEnabled()
                                    ? "linux native V4L2 preferred; OpenCV fallback enabled"
                                    : "linux native V4L2 preferred; OpenCV fallback disabled";
  probe.capabilities.pixel_format = CapabilityState::kBestEffort;
  probe.capabilities.frame_rate = CapabilityState::kBestEffort;
  return probe;
}

} // namespace labops::backends::webcam
