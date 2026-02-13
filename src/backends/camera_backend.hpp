#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace labops::backends {

// Minimal frame representation for backend contract validation.
//
// This is intentionally lightweight for milestone scaffolding; richer metadata
// (byte counts, sequence gaps, transport stats) can be added without changing
// high-level control flow.
struct FrameSample {
  std::uint64_t frame_id = 0;
  std::chrono::system_clock::time_point timestamp{};
  std::uint32_t size_bytes = 0;
  std::optional<bool> dropped;
};

using BackendConfig = std::map<std::string, std::string>;

// Shared camera backend contract used by runtime orchestration.
//
// Contract goals:
// - keep hardware control semantics explicit (`connect/start/stop`)
// - allow deterministic param mutation (`set_param`, `dump_config`)
// - support frame collection by wall-clock duration (`pull_frames`)
class ICameraBackend {
public:
  virtual ~ICameraBackend() = default;

  // Establishes backend connection/session resources.
  virtual bool Connect(std::string& error) = 0;

  // Begins streaming/capture after successful connect.
  virtual bool Start(std::string& error) = 0;

  // Stops active streaming/capture.
  virtual bool Stop(std::string& error) = 0;

  // Updates one backend parameter at a time for controlled experiments.
  virtual bool SetParam(const std::string& key, const std::string& value, std::string& error) = 0;

  // Returns current backend parameter snapshot.
  virtual BackendConfig DumpConfig() const = 0;

  // Collects frames for the requested duration. Implementations should return
  // all available samples for that interval or an error.
  virtual std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration,
                                              std::string& error) = 0;
};

} // namespace labops::backends
