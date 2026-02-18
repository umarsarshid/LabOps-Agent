#pragma once

#include "backends/camera_backend.hpp"
#include "backends/real_sdk/sdk_context.hpp"
#include "backends/real_sdk/stream_session.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::real_sdk {

// Real-backend skeleton for SDK-enabled builds.
//
// Why this exists:
// - gives the repo a concrete `ICameraBackend` implementation for the real path
// - keeps behavior deterministic in OSS until proprietary SDK adapters are
//   integrated outside this repository
// - lets orchestration code depend on the same backend contract across sim and
//   real pipelines
class RealBackend final : public ICameraBackend {
public:
  RealBackend();

  bool Connect(std::string& error) override;
  bool Start(std::string& error) override;
  bool Stop(std::string& error) override;
  bool SetParam(const std::string& key, const std::string& value, std::string& error) override;
  BackendConfig DumpConfig() const override;
  std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration,
                                      std::string& error) override;

private:
  void AppendSdkLog(std::string_view message) const;

  SdkContext sdk_context_;
  StreamSession stream_session_;
  BackendConfig params_;
  std::filesystem::path sdk_log_path_;
  bool connected_ = false;
  bool simulated_disconnect_latched_ = false;
  std::uint64_t next_frame_id_ = 0;
  std::uint64_t pull_calls_ = 0;
  std::optional<std::uint64_t> disconnect_after_pull_calls_;
  std::chrono::system_clock::time_point stream_start_ts_{};
};

} // namespace labops::backends::real_sdk
