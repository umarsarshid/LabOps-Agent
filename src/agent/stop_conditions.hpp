#pragma once

#include "agent/experiment_state.hpp"

#include <cstddef>
#include <string>

namespace labops::agent {

// Enumerates deterministic stop reasons in strict priority order.
// The order is part of the contract because automation depends on one stable
// reason when multiple conditions are true.
enum class StopReason {
  kContinue,
  kMaxRuns,
  kSingleVariableFlip,
  kConfidenceThreshold,
  kStableReproRate,
};

// Stop policy thresholds. Values are intentionally explicit and numeric so
// teams can tune behavior per lab while keeping deterministic decision logic.
struct StopConfig {
  std::size_t max_runs = 12;
  std::size_t stable_repro_window = 4;
  double stable_repro_rate_min = 0.75;
  double confidence_threshold = 0.90;
};

// Dynamic inputs used by stop evaluation.
struct StopInput {
  const ExperimentState* state = nullptr;
  double confidence_score = 0.0;
};

// Deterministic stop decision output with machine- and human-readable context.
struct StopDecision {
  bool should_stop = false;
  StopReason reason = StopReason::kContinue;
  std::string explanation;
  std::size_t run_count = 0;
  std::size_t decisive_run_count = 0;
  std::size_t repro_window_count = 0;
  double observed_repro_rate = 0.0;
  double observed_confidence = 0.0;
  std::string isolating_variable;
};

// Stable string form used in logs/artifacts.
const char* ToString(StopReason reason);

// Evaluates stop conditions in fixed priority order and returns one outcome:
// 1) max_runs
// 2) single-variable flip
// 3) confidence threshold
// 4) stable repro rate
//
// Contract:
// - true: decision is valid and `error` is empty.
// - false: input/config invalid; `error` explains why.
bool EvaluateStopConditions(const StopConfig& config,
                            const StopInput& input,
                            StopDecision& decision,
                            std::string& error);

} // namespace labops::agent
