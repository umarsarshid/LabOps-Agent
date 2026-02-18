#pragma once

#include "core/schema/run_contract.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace labops::events {

// Structured transport anomaly record derived from real-device counter
// snapshots in run metadata.
//
// These findings are used in two places:
// - emitted into events.jsonl as TRANSPORT_ANOMALY timeline records
// - appended into summary top-anomaly callouts for human triage
struct TransportAnomalyFinding {
  std::string heuristic_id;
  std::string counter_name;
  std::uint64_t observed_value = 0;
  std::uint64_t threshold = 0;
  std::string summary;
};

// Evaluates optional transport heuristics from run metadata.
//
// Best-effort contract:
// - if no real-device metadata exists, returns no findings
// - if counters are not available for the SDK/device, returns no findings
// - findings are deterministic and ordered by heuristic priority
std::vector<TransportAnomalyFinding>
DetectTransportAnomalies(const core::schema::RunInfo& run_info);

} // namespace labops::events
