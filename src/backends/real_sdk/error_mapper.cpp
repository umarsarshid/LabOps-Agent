#include "backends/real_sdk/error_mapper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace labops::backends::real_sdk {

namespace {

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string TrimAscii(std::string text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return text.substr(begin, end - begin);
}

std::string CollapseWhitespace(std::string text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previous_was_space = false;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      if (!previous_was_space) {
        normalized.push_back(' ');
      }
      previous_was_space = true;
      continue;
    }
    normalized.push_back(c);
    previous_was_space = false;
  }
  return TrimAscii(std::move(normalized));
}

bool ContainsAny(std::string_view haystack, std::initializer_list<std::string_view> needles) {
  for (const std::string_view needle : needles) {
    if (needle.empty()) {
      continue;
    }
    if (haystack.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

std::string BuildActionableMessage(const RealBackendErrorCode code, std::string_view operation) {
  const std::string operation_label = operation.empty() ? "requested operation" : std::string(operation);

  switch (code) {
  case RealBackendErrorCode::kDeviceBusy:
    return "Device is busy during " + operation_label +
           "; close other camera tools/processes and retry.";
  case RealBackendErrorCode::kTimeout:
    return "Camera timed out during " + operation_label +
           "; check trigger/network conditions and timeout settings.";
  case RealBackendErrorCode::kAccessDenied:
    return "Access denied during " + operation_label +
           "; verify OS permissions and SDK access rights.";
  case RealBackendErrorCode::kDeviceNotFound:
    return "Camera was not found during " + operation_label +
           "; verify power/cable and serial or user_id selector.";
  case RealBackendErrorCode::kDeviceDisconnected:
    return "Camera disconnected during " + operation_label +
           "; check cable/NIC stability and retry.";
  case RealBackendErrorCode::kSdkUnavailable:
    return "Real SDK is unavailable; install/enable SDK and rebuild with LABOPS_ENABLE_REAL_BACKEND=ON.";
  case RealBackendErrorCode::kInvalidConfiguration:
    return "Configuration is invalid for this camera; review scenario values and supported ranges.";
  case RealBackendErrorCode::kStateConflict:
    return "Backend state conflict during " + operation_label +
           "; verify connect/start/stop ordering and active session state.";
  case RealBackendErrorCode::kUnknown:
  default:
    return "Unexpected real-backend failure during " + operation_label +
           "; inspect sdk_log.txt and vendor diagnostics.";
  }
}

RealBackendErrorCode ClassifyFromNormalizedDetail(const std::string& normalized_detail) {
  if (normalized_detail.empty()) {
    return RealBackendErrorCode::kUnknown;
  }

  if (ContainsAny(normalized_detail,
                  {"disabled at build time", "sdk missing", "sdk not found",
                   "sdk adapter is not implemented", "no proprietary sdk adapter",
                   "failed to initialize sdk", "sdk context"})) {
    return RealBackendErrorCode::kSdkUnavailable;
  }

  if (ContainsAny(normalized_detail,
                  {"permission denied", "access denied", "not permitted", "unauthorized"})) {
    return RealBackendErrorCode::kAccessDenied;
  }

  if (ContainsAny(normalized_detail,
                  {"disconnect", "connection lost", "link down", "unplug", "device unavailable"})) {
    return RealBackendErrorCode::kDeviceDisconnected;
  }

  if (ContainsAny(normalized_detail,
                  {"timeout", "timed out", "time out", "deadline exceeded", "wait timeout"})) {
    return RealBackendErrorCode::kTimeout;
  }

  if (ContainsAny(normalized_detail,
                  {"busy", "in use", "resource locked", "already open", "device busy"})) {
    return RealBackendErrorCode::kDeviceBusy;
  }

  if (ContainsAny(normalized_detail,
                  {"no connected cameras", "no camera", "no device", "not found", "not present",
                   "matched selector", "out of range for", "selector"})) {
    return RealBackendErrorCode::kDeviceNotFound;
  }

  if (ContainsAny(normalized_detail,
                  {"already connected", "already running", "not running",
                   "before a successful connect", "stream is stopped", "state"})) {
    return RealBackendErrorCode::kStateConflict;
  }

  if (ContainsAny(normalized_detail, {"parse error", "invalid", "out of range", "type mismatch",
                                      "cannot be empty", "must be"})) {
    return RealBackendErrorCode::kInvalidConfiguration;
  }

  return RealBackendErrorCode::kUnknown;
}

} // namespace

std::string_view ToStableErrorCode(const RealBackendErrorCode code) {
  switch (code) {
  case RealBackendErrorCode::kDeviceBusy:
    return "REAL_DEVICE_BUSY";
  case RealBackendErrorCode::kTimeout:
    return "REAL_TIMEOUT";
  case RealBackendErrorCode::kAccessDenied:
    return "REAL_ACCESS_DENIED";
  case RealBackendErrorCode::kDeviceNotFound:
    return "REAL_DEVICE_NOT_FOUND";
  case RealBackendErrorCode::kDeviceDisconnected:
    return "REAL_DEVICE_DISCONNECTED";
  case RealBackendErrorCode::kSdkUnavailable:
    return "REAL_SDK_UNAVAILABLE";
  case RealBackendErrorCode::kInvalidConfiguration:
    return "REAL_INVALID_CONFIGURATION";
  case RealBackendErrorCode::kStateConflict:
    return "REAL_STATE_CONFLICT";
  case RealBackendErrorCode::kUnknown:
  default:
    return "REAL_UNKNOWN_ERROR";
  }
}

RealBackendErrorMapping MapRealBackendError(std::string_view operation, std::string_view detail) {
  RealBackendErrorMapping mapped;
  mapped.detail = CollapseWhitespace(std::string(detail));
  const std::string normalized_detail = ToLowerAscii(mapped.detail);
  mapped.code = ClassifyFromNormalizedDetail(normalized_detail);
  mapped.actionable_message = BuildActionableMessage(mapped.code, operation);
  return mapped;
}

std::string FormatRealBackendError(std::string_view operation, std::string_view detail) {
  const RealBackendErrorMapping mapped = MapRealBackendError(operation, detail);
  std::string formatted = std::string(ToStableErrorCode(mapped.code)) + ": " + mapped.actionable_message;
  if (!mapped.detail.empty()) {
    formatted += " detail: " + mapped.detail;
  }
  return formatted;
}

} // namespace labops::backends::real_sdk
