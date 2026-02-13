#pragma once

#include "backends/camera_backend.hpp"

#include <chrono>
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
  std::uint32_t ResolveJitterUs(std::string& error) const;
  std::uint32_t ResolveFrameSizeBytes(std::string& error) const;
  std::uint64_t ResolveSeed(std::string& error) const;
  std::uint32_t ResolveDropEveryN(std::string& error) const;
  std::uint32_t ResolveDropPercent(std::string& error) const;
  std::uint32_t ResolveBurstDrop(std::string& error) const;
  std::uint32_t ResolveReorder(std::string& error) const;

  bool connected_ = false;
  bool running_ = false;
  std::uint64_t next_frame_id_ = 0;
  std::chrono::system_clock::time_point stream_start_ts_{};
  BackendConfig params_;
};

} // namespace labops::backends::sim
