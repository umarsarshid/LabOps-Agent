#pragma once

#include "backends/camera_backend.hpp"

#include <memory>
#include <string_view>

namespace labops::backends::real_sdk {

// Returns whether the real backend path is active in the current build.
bool IsRealBackendEnabledAtBuild();

// Returns whether the build requested the real backend path.
// This may still resolve to disabled if SDK discovery failed.
bool WasRealBackendRequestedAtBuild();

// Human-readable status text for CLI visibility.
std::string_view RealBackendAvailabilityStatusText();

// Creates the effective backend object for "real" runs.
// - enabled builds: returns `RealBackend`
// - disabled builds: returns `sdk_stub::RealCameraBackendStub`
std::unique_ptr<ICameraBackend> CreateRealBackend();

} // namespace labops::backends::real_sdk
