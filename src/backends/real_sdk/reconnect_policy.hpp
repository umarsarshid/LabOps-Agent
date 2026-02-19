#pragma once

#include "backends/camera_backend.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace labops::core::logging {
class Logger;
}

namespace labops::backends::real_sdk {

// Stable default budget for one disconnect incident during a run.
constexpr std::uint32_t kDefaultReconnectRetryLimit = 3U;

// Classifies whether a backend error string likely indicates a device-link
// disconnect so run orchestration can choose reconnect handling.
bool IsLikelyDisconnectError(std::string_view error_text);

// Computes remaining reconnect attempts under a fixed retry budget.
std::uint32_t ComputeReconnectAttemptsRemaining(std::uint32_t retry_limit,
                                                std::uint32_t attempts_used_total);

// Result contract for reconnect execution. The caller owns higher-level policy
// decisions (for example, whether to emit a disconnect event before retry).
struct ReconnectAttemptResult {
  bool reconnected = false;
  std::uint32_t attempts_used_total = 0;
  std::string error;
};

// Executes reconnect attempts (`Connect` then `Start`) up to the allowed
// budget for a single disconnect incident.
ReconnectAttemptResult ExecuteReconnectAttempts(backends::ICameraBackend& backend,
                                                std::uint32_t max_attempts_for_disconnect,
                                                std::uint32_t attempts_used_total,
                                                core::logging::Logger& logger);

} // namespace labops::backends::real_sdk
