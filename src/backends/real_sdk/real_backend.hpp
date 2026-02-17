#pragma once

#include "backends/camera_backend.hpp"
#include "backends/real_sdk/sdk_context.hpp"
#include "backends/real_sdk/stream_session.hpp"

#include <chrono>
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
  SdkContext sdk_context_;
  StreamSession stream_session_;
  BackendConfig params_;
  bool connected_ = false;
};

} // namespace labops::backends::real_sdk
