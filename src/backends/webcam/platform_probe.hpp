#pragma once

#include "backends/webcam/capabilities.hpp"

#include <string>

namespace labops::backends::webcam {

// Platform probe result consumed by WebcamBackend construction.
//
// This keeps runtime behavior deterministic today (explicit unavailable reason)
// and gives future platform implementations one place to publish supported
// camera controls.
struct PlatformAvailability {
  bool available = false;
  std::string backend_name = "webcam";
  std::string platform_name = "unknown";
  std::string unavailability_reason = "platform probe not implemented";
  CapabilityModel capabilities;
};

PlatformAvailability ProbePlatformAvailability();

} // namespace labops::backends::webcam
