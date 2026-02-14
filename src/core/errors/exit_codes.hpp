#pragma once

namespace labops::core::errors {

// Stable process-exit contract for CLI automation.
//
// The first three values preserve conventional meanings used by scripts:
// - 0 success
// - 1 generic command failure
// - 2 usage/argument failure
//
// Additional values classify common operational failure modes so CI and
// wrappers can branch without scraping stderr text.
enum class ExitCode : int {
  kSuccess = 0,
  kFailure = 1,
  kUsage = 2,
  kSchemaInvalid = 10,
  kBackendConnectFailed = 20,
  kThresholdsFailed = 30,
};

constexpr int ToInt(ExitCode code) {
  return static_cast<int>(code);
}

} // namespace labops::core::errors
