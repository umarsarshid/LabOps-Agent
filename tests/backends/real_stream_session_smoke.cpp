#include "backends/real_sdk/real_backend.hpp"
#include "backends/real_sdk/sdk_context.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

std::string FindOrEmpty(const labops::backends::BackendConfig& config, const std::string& key) {
  const auto it = config.find(key);
  if (it == config.end()) {
    return {};
  }
  return it->second;
}

void AssertConfigState(const labops::backends::real_sdk::RealBackend& backend,
                       std::string_view expected_connected, std::string_view expected_running) {
  const auto config = backend.DumpConfig();
  if (FindOrEmpty(config, "connected") != expected_connected) {
    Fail("unexpected connected marker in real backend dump config");
  }
  if (FindOrEmpty(config, "running") != expected_running) {
    Fail("unexpected running marker in real backend dump config");
  }
}

} // namespace

int main() {
  using labops::backends::real_sdk::RealBackend;
  using labops::backends::real_sdk::SdkContext;

  SdkContext::DebugResetForTests();

  constexpr int kIterations = 5;
  for (int i = 0; i < kIterations; ++i) {
    std::string error;
    {
      RealBackend backend;
      AssertConfigState(backend, "false", "false");

      if (!backend.Connect(error)) {
        Fail("expected real backend connect to succeed with SDK context placeholder");
      }
      AssertConfigState(backend, "true", "false");

      if (!backend.Start(error)) {
        Fail("expected first start to succeed");
      }
      AssertConfigState(backend, "true", "true");

      if (!backend.Stop(error)) {
        Fail("expected first stop to succeed");
      }
      AssertConfigState(backend, "true", "false");

      if (!backend.Stop(error)) {
        Fail("expected stop to be idempotent");
      }
      AssertConfigState(backend, "true", "false");

      if (!backend.Start(error)) {
        Fail("expected restart after idempotent stop to succeed");
      }
      AssertConfigState(backend, "true", "true");

      std::string pull_error;
      const auto frames = backend.PullFrames(std::chrono::milliseconds(250), pull_error);
      if (!pull_error.empty()) {
        Fail("expected real backend pull_frames to succeed while running");
      }
      if (frames.empty()) {
        Fail("expected real backend pull_frames to return samples");
      }

      if (!backend.Stop(error)) {
        Fail("expected stop after pull attempt to succeed");
      }
      AssertConfigState(backend, "true", "false");
    }

    const auto snapshot = SdkContext::DebugSnapshot();
    if (snapshot.initialized || snapshot.active_handles != 0U) {
      Fail("expected no active SDK handles after backend teardown");
    }
  }

  const auto snapshot = SdkContext::DebugSnapshot();
  if (snapshot.init_calls != static_cast<std::uint64_t>(kIterations)) {
    Fail("unexpected SDK init call count for repeated backend runs");
  }
  if (snapshot.shutdown_calls != static_cast<std::uint64_t>(kIterations)) {
    Fail("unexpected SDK shutdown call count for repeated backend runs");
  }

  return 0;
}
