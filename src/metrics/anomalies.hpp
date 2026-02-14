#pragma once

#include "metrics/fps.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace labops::metrics {

// Builds a short anomaly list for run summaries.
//
// Heuristics currently include:
// - resend spike
// - jitter cliff
// - periodic stall
//
// Contract:
// - returns a deterministic list ordered by heuristic priority.
// - includes threshold-failure notes when provided.
// - list is capped for concise run-summary readability.
std::vector<std::string> BuildAnomalyHighlights(const FpsReport& report,
                                                std::uint32_t configured_fps,
                                                const std::vector<std::string>& threshold_failures);

} // namespace labops::metrics
