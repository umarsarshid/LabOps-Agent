#pragma once

#include "backends/camera_backend.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace labops::backends::sdk_stub {

// Returns true when the build enables the real-backend integration path.
//
// Important: this does not mean proprietary SDK adapters are present. In this
// repo, the adapter remains a stub even when enabled.
bool IsRealBackendEnabledAtBuild();

// Returns true when build configuration requested real-backend support.
// This may still evaluate to disabled if SDK discovery failed at configure time.
bool WasRealBackendRequestedAtBuild();

// Human-readable status string for CLI visibility.
// Values today:
// - "enabled"
// - "disabled (SDK not found)"
// - "disabled (build option OFF)"
std::string_view RealBackendAvailabilityStatusText();

// Non-proprietary placeholder backend for future SDK integration.
//
// Why this exists:
// - keeps a stable compile-time integration boundary for real hardware paths
// - avoids shipping vendor headers/binaries in this repository
// - lets CI verify Linux/macOS/Windows builds without any SDK installed
class RealCameraBackendStub final : public ICameraBackend {
public:
  RealCameraBackendStub();

  bool Connect(std::string& error) override;
  bool Start(std::string& error) override;
  bool Stop(std::string& error) override;
  bool SetParam(const std::string& key, const std::string& value, std::string& error) override;
  BackendConfig DumpConfig() const override;
  std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration,
                                      std::string& error) override;

private:
  void AppendSdkLog(std::string_view message) const;

  BackendConfig params_;
  std::filesystem::path sdk_log_path_;
  bool connected_ = false;
  bool running_ = false;
};

} // namespace labops::backends::sdk_stub
