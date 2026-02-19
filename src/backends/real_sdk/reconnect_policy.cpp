#include "backends/real_sdk/reconnect_policy.hpp"

#include "backends/real_sdk/error_mapper.hpp"
#include "core/logging/logger.hpp"

#include <algorithm>
#include <cctype>

namespace labops::backends::real_sdk {

namespace {

struct RealFailureDetails {
  std::string code;
  std::string actionable_message;
  std::string formatted_message;
};

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

RealFailureDetails MapRealFailure(std::string_view operation, std::string_view raw_error) {
  const auto mapped = MapRealBackendError(operation, raw_error);
  RealFailureDetails details;
  details.code = std::string(ToStableErrorCode(mapped.code));
  details.actionable_message = mapped.actionable_message;
  details.formatted_message = FormatRealBackendError(operation, raw_error);
  return details;
}

} // namespace

bool IsLikelyDisconnectError(std::string_view error_text) {
  if (error_text.empty()) {
    return false;
  }
  const std::string normalized = ToLowerAscii(std::string(error_text));
  return normalized.find("disconnect") != std::string::npos ||
         normalized.find("connection lost") != std::string::npos ||
         normalized.find("link down") != std::string::npos;
}

std::uint32_t ComputeReconnectAttemptsRemaining(const std::uint32_t retry_limit,
                                                const std::uint32_t attempts_used_total) {
  if (attempts_used_total >= retry_limit) {
    return 0U;
  }
  return retry_limit - attempts_used_total;
}

ReconnectAttemptResult ExecuteReconnectAttempts(backends::ICameraBackend& backend,
                                                const std::uint32_t max_attempts_for_disconnect,
                                                const std::uint32_t attempts_used_total,
                                                core::logging::Logger& logger) {
  ReconnectAttemptResult result;
  result.attempts_used_total = attempts_used_total;

  if (max_attempts_for_disconnect == 0U) {
    result.error = "reconnect attempts exhausted";
    return result;
  }

  for (std::uint32_t attempt = 1; attempt <= max_attempts_for_disconnect; ++attempt) {
    ++result.attempts_used_total;
    std::string connect_error;
    if (!backend.Connect(connect_error)) {
      const RealFailureDetails mapped = MapRealFailure("connect", connect_error);
      logger.Warn("reconnect attempt connect failed",
                  {{"attempt", std::to_string(attempt)},
                   {"attempts_used_total", std::to_string(result.attempts_used_total)},
                   {"max_attempts_for_disconnect", std::to_string(max_attempts_for_disconnect)},
                   {"error_code", mapped.code},
                   {"error_action", mapped.actionable_message},
                   {"error", connect_error}});
      result.error = mapped.formatted_message;
      continue;
    }

    std::string start_error;
    if (!backend.Start(start_error)) {
      const RealFailureDetails mapped = MapRealFailure("start", start_error);
      logger.Warn("reconnect attempt start failed",
                  {{"attempt", std::to_string(attempt)},
                   {"attempts_used_total", std::to_string(result.attempts_used_total)},
                   {"max_attempts_for_disconnect", std::to_string(max_attempts_for_disconnect)},
                   {"error_code", mapped.code},
                   {"error_action", mapped.actionable_message},
                   {"error", start_error}});
      std::string stop_error;
      (void)backend.Stop(stop_error);
      result.error = mapped.formatted_message;
      continue;
    }

    logger.Info("reconnect attempt succeeded",
                {{"attempt", std::to_string(attempt)},
                 {"attempts_used_total", std::to_string(result.attempts_used_total)}});
    result.reconnected = true;
    result.error.clear();
    return result;
  }

  if (result.error.empty()) {
    result.error = "reconnect attempts exhausted";
  }
  return result;
}

} // namespace labops::backends::real_sdk
