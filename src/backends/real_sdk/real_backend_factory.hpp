#pragma once

#include "backends/camera_backend.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::real_sdk {

// Normalized camera identity data used by CLI and future artifact capture.
struct DeviceInfo {
  std::string model;
  std::string serial;
  std::string user_id;
  std::string transport;
  std::optional<std::string> ip_address;
  std::optional<std::string> mac_address;
};

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

// Enumerates connected real cameras and maps SDK descriptors into DeviceInfo.
//
// In this repository, proprietary SDK calls are replaced by a local descriptor
// fixture path (`LABOPS_REAL_DEVICE_FIXTURE`) so discovery behavior can be
// verified in CI and local builds without vendor binaries.
bool EnumerateConnectedDevices(std::vector<DeviceInfo>& devices, std::string& error);

} // namespace labops::backends::real_sdk
