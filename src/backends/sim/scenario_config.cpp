#include "backends/sim/scenario_config.hpp"

namespace labops::backends::sim {

namespace {

bool ApplyParam(ICameraBackend& backend, const std::string& key, const std::string& value,
                std::string& error) {
  if (!backend.SetParam(key, value, error)) {
    if (error.empty()) {
      error = "failed to set param: " + key;
    }
    return false;
  }
  return true;
}

} // namespace

bool ApplyScenarioConfig(ICameraBackend& backend, const SimScenarioConfig& config,
                         std::string& error) {
  // Keep validation close to scenario-to-backend translation so invalid knobs
  // fail fast before run execution starts.
  if (config.faults.drop_percent > 100U) {
    error = "drop_percent must be in range [0,100]";
    return false;
  }

  if (!ApplyParam(backend, "fps", std::to_string(config.fps), error)) {
    return false;
  }
  if (!ApplyParam(backend, "jitter_us", std::to_string(config.jitter_us), error)) {
    return false;
  }
  if (!ApplyParam(backend, "seed", std::to_string(config.seed), error)) {
    return false;
  }
  if (!ApplyParam(backend, "frame_size_bytes", std::to_string(config.frame_size_bytes), error)) {
    return false;
  }
  if (!ApplyParam(backend, "drop_every_n", std::to_string(config.drop_every_n), error)) {
    return false;
  }
  if (!ApplyParam(backend, "drop_percent", std::to_string(config.faults.drop_percent), error)) {
    return false;
  }
  if (!ApplyParam(backend, "burst_drop", std::to_string(config.faults.burst_drop), error)) {
    return false;
  }
  if (!ApplyParam(backend, "reorder", std::to_string(config.faults.reorder), error)) {
    return false;
  }

  return true;
}

} // namespace labops::backends::sim
