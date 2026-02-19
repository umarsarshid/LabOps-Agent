#include "backends/webcam/webcam_factory.hpp"

#include "backends/webcam/platform_probe.hpp"
#include "backends/webcam/webcam_backend.hpp"

namespace labops::backends::webcam {

namespace {

bool IsCompiledForCurrentPlatform() {
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
  return true;
#else
  return false;
#endif
}

} // namespace

WebcamBackendAvailability GetWebcamBackendAvailability() {
  WebcamBackendAvailability availability;
  availability.compiled = IsCompiledForCurrentPlatform();
  if (!availability.compiled) {
    availability.available = false;
    availability.platform = "unknown";
    availability.reason = "webcam backend not compiled on this platform";
    return availability;
  }

  const PlatformAvailability probe = ProbePlatformAvailability();
  availability.available = probe.available;
  availability.platform = probe.platform_name;
  availability.reason = probe.available ? "enabled" : probe.unavailability_reason;
  return availability;
}

std::unique_ptr<ICameraBackend> CreateWebcamBackend() {
  if (!IsCompiledForCurrentPlatform()) {
    return nullptr;
  }
  return std::make_unique<WebcamBackend>();
}

} // namespace labops::backends::webcam
