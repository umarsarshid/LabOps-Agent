#include "backends/real_sdk/real_backend_factory.hpp"

#include "backends/real_sdk/real_backend.hpp"
#include "backends/sdk_stub/real_camera_backend_stub.hpp"

namespace labops::backends::real_sdk {

bool IsRealBackendEnabledAtBuild() {
  return sdk_stub::IsRealBackendEnabledAtBuild();
}

bool WasRealBackendRequestedAtBuild() {
  return sdk_stub::WasRealBackendRequestedAtBuild();
}

std::string_view RealBackendAvailabilityStatusText() {
  return sdk_stub::RealBackendAvailabilityStatusText();
}

std::unique_ptr<ICameraBackend> CreateRealBackend() {
  if (IsRealBackendEnabledAtBuild()) {
    return std::make_unique<RealBackend>();
  }
  return std::make_unique<sdk_stub::RealCameraBackendStub>();
}

} // namespace labops::backends::real_sdk
