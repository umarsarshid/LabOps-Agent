#include "backends/camera_backend.hpp"
#include "backends/sdk_stub/real_camera_backend_stub.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <type_traits>

#ifndef LABOPS_ENABLE_REAL_BACKEND
#define LABOPS_ENABLE_REAL_BACKEND 0
#endif

#ifndef LABOPS_REAL_BACKEND_REQUESTED
#define LABOPS_REAL_BACKEND_REQUESTED 0
#endif

int main() {
  using labops::backends::ICameraBackend;
  using labops::backends::sdk_stub::IsRealBackendEnabledAtBuild;
  using labops::backends::sdk_stub::RealBackendAvailabilityStatusText;
  using labops::backends::sdk_stub::RealCameraBackendStub;
  using labops::backends::sdk_stub::WasRealBackendRequestedAtBuild;

  static_assert(std::is_base_of_v<ICameraBackend, RealCameraBackendStub>,
                "real backend stub must implement ICameraBackend");

  RealCameraBackendStub backend;

  const bool expected_enabled = LABOPS_ENABLE_REAL_BACKEND != 0;
  const bool expected_requested = LABOPS_REAL_BACKEND_REQUESTED != 0;
  if (IsRealBackendEnabledAtBuild() != expected_enabled) {
    std::cerr << "build-flag mismatch: helper does not reflect LABOPS_ENABLE_REAL_BACKEND\n";
    return 1;
  }
  if (WasRealBackendRequestedAtBuild() != expected_requested) {
    std::cerr << "build-flag mismatch: helper does not reflect LABOPS_REAL_BACKEND_REQUESTED\n";
    return 1;
  }

  const std::string expected_status_text =
      expected_enabled
          ? "enabled"
          : (expected_requested ? "disabled (SDK not found)" : "disabled (build option OFF)");
  if (RealBackendAvailabilityStatusText() != expected_status_text) {
    std::cerr << "unexpected availability status text. expected='" << expected_status_text
              << "' actual='" << RealBackendAvailabilityStatusText() << "'\n";
    return 1;
  }

  std::string error;
  if (backend.Connect(error)) {
    std::cerr << "expected real backend stub connect to fail in OSS build\n";
    return 1;
  }
  if (error.empty()) {
    std::cerr << "expected actionable connect error message\n";
    return 1;
  }

  const auto config = backend.DumpConfig();
  auto find_or_empty = [&](const std::string& key) {
    auto it = config.find(key);
    if (it == config.end()) {
      return std::string{};
    }
    return it->second;
  };

  if (find_or_empty("backend") != "real_stub") {
    std::cerr << "expected backend=real_stub in dumped config\n";
    return 1;
  }
  if (find_or_empty("sdk_adapter") != "not_integrated") {
    std::cerr << "expected sdk_adapter=not_integrated in dumped config\n";
    return 1;
  }
  if (find_or_empty("build_real_backend_enabled") != (expected_enabled ? "true" : "false")) {
    std::cerr << "expected build flag marker in dumped config\n";
    return 1;
  }

  std::string pull_error;
  const auto frames = backend.PullFrames(std::chrono::milliseconds(100), pull_error);
  if (!frames.empty()) {
    std::cerr << "expected no frames from sdk stub\n";
    return 1;
  }
  if (pull_error.empty()) {
    std::cerr << "expected actionable pull_frames error from sdk stub\n";
    return 1;
  }

  return 0;
}
