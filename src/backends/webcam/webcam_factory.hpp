#pragma once

#include "backends/camera_backend.hpp"

#include <memory>
#include <string>

namespace labops::backends::webcam {

// Availability snapshot used by CLI status reporting.
//
// Semantics:
// - `compiled` answers whether webcam backend code is expected to run on this
//   OS target.
// - `available` answers whether runtime capture path is actually ready now.
// - `reason` is always populated when unavailable so operators get actionable
//   messaging from `labops list-backends`.
struct WebcamBackendAvailability {
  bool compiled = false;
  bool available = false;
  std::string reason;
  std::string platform;
};

// Returns the current webcam backend availability status.
WebcamBackendAvailability GetWebcamBackendAvailability();

// Creates the webcam backend implementation when compiled for this platform.
// Returns nullptr when the backend is not compiled on the current target.
std::unique_ptr<ICameraBackend> CreateWebcamBackend();

} // namespace labops::backends::webcam
