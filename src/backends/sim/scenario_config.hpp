#pragma once

#include "backends/camera_backend.hpp"

#include <cstdint>
#include <string>

namespace labops::backends::sim {

// Fault injection knobs controlled by scenario-level config.
struct SimFaultConfig {
  std::uint32_t drop_percent = 0; // 0..100
  std::uint32_t burst_drop = 0;   // consecutive dropped frames once triggered
  std::uint32_t reorder = 0;      // reorder window size (0/1 disables)
};

// Scenario-facing config for deterministic sim execution.
//
// These values map directly to backend params so scenario loaders can remain
// declarative and backend-independent orchestration code can stay simple.
struct SimScenarioConfig {
  std::uint32_t fps = 30;
  std::uint32_t jitter_us = 0;
  std::uint64_t seed = 1;
  std::uint32_t frame_size_bytes = 1'048'576;
  std::uint32_t drop_every_n = 0;
  SimFaultConfig faults;
};

// Applies scenario config to any backend implementing `set_param`.
bool ApplyScenarioConfig(ICameraBackend& backend, const SimScenarioConfig& config,
                         std::string& error);

} // namespace labops::backends::sim
