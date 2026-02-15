#include "backends/camera_backend.hpp"
#include "backends/real_sdk/real_backend.hpp"
#include "backends/real_sdk/real_backend_factory.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#ifndef LABOPS_ENABLE_REAL_BACKEND
#define LABOPS_ENABLE_REAL_BACKEND 0
#endif

#ifndef LABOPS_REAL_BACKEND_REQUESTED
#define LABOPS_REAL_BACKEND_REQUESTED 0
#endif

namespace {

void Fail(const std::string& message) {
  std::cerr << message << '\n';
  std::abort();
}

std::string FindOrEmpty(const labops::backends::BackendConfig& config, const std::string& key) {
  auto it = config.find(key);
  if (it == config.end()) {
    return {};
  }
  return it->second;
}

} // namespace

int main() {
  using labops::backends::ICameraBackend;
  using labops::backends::real_sdk::CreateRealBackend;
  using labops::backends::real_sdk::IsRealBackendEnabledAtBuild;
  using labops::backends::real_sdk::RealBackend;
  using labops::backends::real_sdk::RealBackendAvailabilityStatusText;
  using labops::backends::real_sdk::WasRealBackendRequestedAtBuild;

  static_assert(std::is_base_of_v<ICameraBackend, RealBackend>,
                "real backend skeleton must implement ICameraBackend");

  const bool expected_enabled = LABOPS_ENABLE_REAL_BACKEND != 0;
  const bool expected_requested = LABOPS_REAL_BACKEND_REQUESTED != 0;
  if (IsRealBackendEnabledAtBuild() != expected_enabled) {
    Fail("build-flag mismatch: helper does not reflect LABOPS_ENABLE_REAL_BACKEND");
  }
  if (WasRealBackendRequestedAtBuild() != expected_requested) {
    Fail("build-flag mismatch: helper does not reflect LABOPS_REAL_BACKEND_REQUESTED");
  }

  const std::string expected_status_text =
      expected_enabled
          ? "enabled"
          : (expected_requested ? "disabled (SDK not found)" : "disabled (build option OFF)");
  if (RealBackendAvailabilityStatusText() != expected_status_text) {
    Fail("unexpected status text from real backend factory helper");
  }

  std::unique_ptr<ICameraBackend> backend = CreateRealBackend();
  if (!backend) {
    Fail("CreateRealBackend returned null backend");
  }

  const auto config = backend->DumpConfig();
  if (expected_enabled) {
    if (FindOrEmpty(config, "backend") != "real") {
      Fail("expected backend=real when real backend is enabled");
    }
    if (FindOrEmpty(config, "integration_stage") != "skeleton") {
      Fail("expected integration_stage=skeleton for real backend");
    }
  } else {
    if (FindOrEmpty(config, "backend") != "real_stub") {
      Fail("expected backend=real_stub fallback when real backend is disabled");
    }
  }

  std::string error;
  if (backend->Connect(error)) {
    Fail("expected CreateRealBackend result to fail connect until SDK adapter is implemented");
  }
  if (error.empty()) {
    Fail("expected actionable connect error from CreateRealBackend result");
  }

  std::string pull_error;
  const auto frames = backend->PullFrames(std::chrono::milliseconds(100), pull_error);
  if (!frames.empty()) {
    Fail("expected no frames from current real backend implementation");
  }
  if (pull_error.empty()) {
    Fail("expected actionable pull_frames error from current real backend implementation");
  }

  return 0;
}
