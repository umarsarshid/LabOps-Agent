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

// Flexible selector contract used by CLI and scenario files.
// Supported keys:
// - serial:<value>
// - user_id:<value>
// - index:<n> (0-based)
//
// Selectors may combine identity + index (for tie-break):
// - serial:ABC123,index:1
// - user_id:LineCam,index:0
struct DeviceSelector {
  std::optional<std::string> serial;
  std::optional<std::string> user_id;
  std::optional<std::size_t> index;
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

// Parses selector strings used by `--device` and scenario `device_selector`.
bool ParseDeviceSelector(std::string_view selector_text, DeviceSelector& selector,
                         std::string& error);

// Resolves one device from a device list using a parsed selector.
bool ResolveDeviceSelector(const std::vector<DeviceInfo>& devices, const DeviceSelector& selector,
                           DeviceInfo& selected, std::size_t& selected_index, std::string& error);

// Convenience: enumerate connected devices and resolve selector in one call.
bool ResolveConnectedDevice(std::string_view selector_text, DeviceInfo& selected,
                            std::size_t& selected_index, std::string& error);

} // namespace labops::backends::real_sdk
