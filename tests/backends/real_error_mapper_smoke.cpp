#include "../common/assertions.hpp"
#include "backends/real_sdk/error_mapper.hpp"

#include <string_view>

int main() {
  using labops::backends::real_sdk::FormatRealBackendError;
  using labops::backends::real_sdk::MapRealBackendError;
  using labops::backends::real_sdk::RealBackendErrorCode;
  using labops::backends::real_sdk::ToStableErrorCode;
  using labops::tests::common::AssertContains;
  using labops::tests::common::Fail;

  {
    const auto mapped = MapRealBackendError("connect", "device busy: owned by another process");
    if (mapped.code != RealBackendErrorCode::kDeviceBusy) {
      Fail("expected busy classification for busy error text");
    }
    if (ToStableErrorCode(mapped.code) != "REAL_DEVICE_BUSY") {
      Fail("unexpected stable code for busy classification");
    }
  }

  {
    const auto mapped = MapRealBackendError("pull_frames", "frame wait timeout after 1000 ms");
    if (mapped.code != RealBackendErrorCode::kTimeout) {
      Fail("expected timeout classification for timeout error text");
    }
    if (ToStableErrorCode(mapped.code) != "REAL_TIMEOUT") {
      Fail("unexpected stable code for timeout classification");
    }
  }

  {
    const auto mapped =
        MapRealBackendError("pull_frames", "device disconnected during acquisition");
    if (mapped.code != RealBackendErrorCode::kDeviceDisconnected) {
      Fail("expected disconnect classification for disconnect error text");
    }
    if (ToStableErrorCode(mapped.code) != "REAL_DEVICE_DISCONNECTED") {
      Fail("unexpected stable code for disconnect classification");
    }
  }

  {
    const auto mapped = MapRealBackendError(
        "connect",
        "real backend path is disabled at build time (set -DLABOPS_ENABLE_REAL_BACKEND=ON)");
    if (mapped.code != RealBackendErrorCode::kSdkUnavailable) {
      Fail("expected sdk-unavailable classification when build disables real backend");
    }
    if (ToStableErrorCode(mapped.code) != "REAL_SDK_UNAVAILABLE") {
      Fail("unexpected stable code for sdk unavailable classification");
    }
  }

  {
    const auto mapped = MapRealBackendError("set_param", "invalid value for ExposureTime");
    if (mapped.code != RealBackendErrorCode::kInvalidConfiguration) {
      Fail("expected invalid-configuration classification for invalid value text");
    }
    if (ToStableErrorCode(mapped.code) != "REAL_INVALID_CONFIGURATION") {
      Fail("unexpected stable code for invalid configuration classification");
    }
  }

  {
    const std::string formatted =
        FormatRealBackendError("connect", "permission denied while opening camera");
    AssertContains(formatted, "REAL_ACCESS_DENIED");
    AssertContains(formatted, "Access denied during connect");
    AssertContains(formatted, "detail: permission denied while opening camera");
  }

  {
    const std::string formatted = FormatRealBackendError("start", "");
    AssertContains(formatted, "REAL_UNKNOWN_ERROR");
    AssertContains(formatted, "Unexpected real-backend failure during start");
  }

  return 0;
}
