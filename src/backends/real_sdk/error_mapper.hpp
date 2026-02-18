#pragma once

#include <string>
#include <string_view>

namespace labops::backends::real_sdk {

// Stable classification for real-backend failures.
//
// Why this exists:
// - backend/raw SDK strings are vendor-specific and can change over time
// - automation and engineers need stable, grep-friendly codes
// - run/list-devices surfaces should stay actionable without parsing internals
enum class RealBackendErrorCode {
  kDeviceBusy,
  kTimeout,
  kAccessDenied,
  kDeviceNotFound,
  kDeviceDisconnected,
  kSdkUnavailable,
  kInvalidConfiguration,
  kStateConflict,
  kUnknown,
};

std::string_view ToStableErrorCode(RealBackendErrorCode code);

struct RealBackendErrorMapping {
  RealBackendErrorCode code = RealBackendErrorCode::kUnknown;
  std::string actionable_message;
  std::string detail;
};

// Maps raw backend/SDK error text to a stable code and human-actionable message.
// `operation` is a human label like "connect", "start", "pull_frames", or
// "device_discovery" used to keep guidance specific.
RealBackendErrorMapping MapRealBackendError(std::string_view operation, std::string_view detail);

// Returns single-line contract text:
//   "<STABLE_CODE>: <actionable_message> detail: <raw_detail>"
// The detail suffix is omitted when raw detail is empty.
std::string FormatRealBackendError(std::string_view operation, std::string_view detail);

} // namespace labops::backends::real_sdk
