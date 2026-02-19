#include "backends/webcam/platform_probe.hpp"

#if defined(__linux__)
#include "backends/webcam/linux/platform_probe_linux.hpp"
#elif defined(__APPLE__)
#include "backends/webcam/macos/platform_probe_macos.hpp"
#elif defined(_WIN32)
#include "backends/webcam/windows/platform_probe_windows.hpp"
#endif

namespace labops::backends::webcam {

PlatformAvailability ProbePlatformAvailability() {
#if defined(__linux__)
  return ProbePlatformAvailabilityLinux();
#elif defined(__APPLE__)
  return ProbePlatformAvailabilityMacos();
#elif defined(_WIN32)
  return ProbePlatformAvailabilityWindows();
#else
  PlatformAvailability probe;
  probe.platform_name = "unknown";
  probe.unavailability_reason = "unsupported operating system";
  return probe;
#endif
}

} // namespace labops::backends::webcam
