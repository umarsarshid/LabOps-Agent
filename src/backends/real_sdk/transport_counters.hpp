#pragma once

#include "backends/camera_backend.hpp"

#include <cstdint>
#include <string>

namespace labops::backends::real_sdk {

// Normalized snapshot for transport-layer counters gathered from SDK/backend
// config dumps. Collection is best-effort: missing or non-numeric values are
// treated as "not available" instead of hard failures.
struct TransportCounterReading {
  bool available = false;
  std::uint64_t value = 0;
  std::string source_key;
};

struct TransportCountersSnapshot {
  TransportCounterReading resends;
  TransportCounterReading packet_errors;
  TransportCounterReading dropped_packets;
};

// Collects transport counters from backend dump keys using common alias sets.
//
// Why this exists:
// - different SDKs expose different node names for the same transport counters
// - run orchestration needs one stable, backend-agnostic shape for run.json
// - best-effort collection should never fail a run
TransportCountersSnapshot CollectTransportCounters(const BackendConfig& backend_dump);

} // namespace labops::backends::real_sdk
