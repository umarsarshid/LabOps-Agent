#include "../common/assertions.hpp"
#include "backends/camera_backend.hpp"
#include "backends/real_sdk/reconnect_policy.hpp"
#include "core/logging/logger.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace {

using labops::backends::BackendConfig;
using labops::backends::FrameSample;

class ScriptedBackend final : public labops::backends::ICameraBackend {
public:
  struct StepResult {
    bool ok = true;
    std::string error;
  };

  std::vector<StepResult> connect_script;
  std::vector<StepResult> start_script;
  std::uint32_t connect_calls = 0;
  std::uint32_t start_calls = 0;
  std::uint32_t stop_calls = 0;

  bool Connect(std::string& error) override {
    ++connect_calls;
    return ConsumeStep(connect_script, connect_calls, error);
  }

  bool Start(std::string& error) override {
    ++start_calls;
    return ConsumeStep(start_script, start_calls, error);
  }

  bool Stop(std::string& error) override {
    ++stop_calls;
    error.clear();
    return true;
  }

  bool SetParam(const std::string&, const std::string&, std::string& error) override {
    error = "not used in test";
    return false;
  }

  BackendConfig DumpConfig() const override {
    return {};
  }

  std::vector<FrameSample> PullFrames(std::chrono::milliseconds, std::string& error) override {
    error = "not used in test";
    return {};
  }

private:
  static bool ConsumeStep(const std::vector<StepResult>& script, const std::uint32_t call_number,
                          std::string& error) {
    if (call_number == 0U || call_number > script.size()) {
      error = "unexpected scripted call";
      return false;
    }
    const StepResult& step = script[call_number - 1U];
    error = step.error;
    return step.ok;
  }
};

} // namespace

int main() {
  using labops::backends::real_sdk::ComputeReconnectAttemptsRemaining;
  using labops::backends::real_sdk::ExecuteReconnectAttempts;
  using labops::backends::real_sdk::IsLikelyDisconnectError;
  using labops::core::logging::Logger;
  using labops::core::logging::LogLevel;
  using labops::tests::common::AssertContains;
  using labops::tests::common::Fail;

  if (!IsLikelyDisconnectError("device disconnected during acquisition")) {
    Fail("expected disconnect keyword to classify as disconnect");
  }
  if (!IsLikelyDisconnectError("connection lost while pulling frames")) {
    Fail("expected connection lost keyword to classify as disconnect");
  }
  if (IsLikelyDisconnectError("invalid value for ExposureTime")) {
    Fail("expected config error to avoid disconnect classification");
  }

  if (ComputeReconnectAttemptsRemaining(3U, 0U) != 3U) {
    Fail("remaining reconnect attempts mismatch at zero used");
  }
  if (ComputeReconnectAttemptsRemaining(3U, 2U) != 1U) {
    Fail("remaining reconnect attempts mismatch at partial usage");
  }
  if (ComputeReconnectAttemptsRemaining(3U, 3U) != 0U) {
    Fail("remaining reconnect attempts mismatch at exhausted usage");
  }
  if (ComputeReconnectAttemptsRemaining(3U, 5U) != 0U) {
    Fail("remaining reconnect attempts mismatch above retry limit");
  }

  {
    ScriptedBackend backend;
    backend.connect_script = {
        {.ok = false, .error = "device disconnected during acquisition"},
        {.ok = true, .error = ""},
        {.ok = true, .error = ""},
    };
    backend.start_script = {
        {.ok = false, .error = "start failed because link was unstable"},
        {.ok = true, .error = ""},
    };

    Logger logger(LogLevel::kError);
    const auto result = ExecuteReconnectAttempts(backend, 3U, 1U, logger);
    if (!result.reconnected) {
      Fail("expected reconnect policy to succeed within configured budget");
    }
    if (result.attempts_used_total != 4U) {
      Fail("expected attempts_used_total to include prior and policy attempts");
    }
    if (!result.error.empty()) {
      Fail("expected reconnect success to clear error text");
    }
    if (backend.connect_calls != 3U) {
      Fail("expected three connect calls for scripted reconnect success flow");
    }
    if (backend.start_calls != 2U) {
      Fail("expected two start calls for scripted reconnect success flow");
    }
    if (backend.stop_calls != 1U) {
      Fail("expected one stop call after failed start in reconnect flow");
    }
  }

  {
    ScriptedBackend backend;
    backend.connect_script = {
        {.ok = false, .error = "device disconnected during acquisition"},
        {.ok = false, .error = "device disconnected during acquisition"},
    };

    Logger logger(LogLevel::kError);
    const auto result = ExecuteReconnectAttempts(backend, 2U, 0U, logger);
    if (result.reconnected) {
      Fail("expected reconnect policy failure after budget exhaustion");
    }
    if (result.attempts_used_total != 2U) {
      Fail("expected attempts_used_total to match exhausted reconnect budget");
    }
    AssertContains(result.error, "REAL_DEVICE_DISCONNECTED");
    if (backend.connect_calls != 2U) {
      Fail("expected two connect calls for exhausted reconnect flow");
    }
    if (backend.start_calls != 0U) {
      Fail("expected start not to run when connect keeps failing");
    }
    if (backend.stop_calls != 0U) {
      Fail("expected stop not to run when start never executes");
    }
  }

  return 0;
}
