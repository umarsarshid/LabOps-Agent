#pragma once

#include "backends/camera_backend.hpp"

#include <cstdint>
#include <string>

namespace labops::backends::sim {

// Deterministic, hardware-free backend implementation.
//
// This class is intentionally strict about state transitions so CLI/runtime
// code can exercise realistic control flow before real SDK integration.
class SimCameraBackend final : public ICameraBackend {
public:
  SimCameraBackend();

  bool Connect(std::string& error) override;
  bool Start(std::string& error) override;
  bool Stop(std::string& error) override;
  bool SetParam(const std::string& key, const std::string& value, std::string& error) override;
  BackendConfig DumpConfig() const override;
  std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration,
                                      std::string& error) override;

private:
  std::uint32_t ResolveFps(std::string& error) const;

  bool connected_ = false;
  bool running_ = false;
  std::uint64_t next_frame_index_ = 0;
  BackendConfig params_;
};

} // namespace labops::backends::sim
