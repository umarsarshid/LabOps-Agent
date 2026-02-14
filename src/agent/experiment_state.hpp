#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace labops::agent {

// Hypothesis lifecycle stays explicit so the agent can explain whether a theory
// is still open, supported by evidence, rejected, or blocked on human review.
enum class HypothesisStatus {
  kOpen,
  kSupported,
  kRejected,
  kNeedsHuman,
};

// Row-level outcome for the experiment results table. This keeps machine
// parsing and human summaries aligned on a small, stable vocabulary.
enum class ResultStatus {
  kPass,
  kFail,
  kInconclusive,
};

// A single root-cause hypothesis tracked by the agent.
struct Hypothesis {
  std::string id;
  std::string statement;
  std::string variable_name;
  HypothesisStatus status = HypothesisStatus::kOpen;
};

// Tracks one variable mutation the agent has already tried. Keeping baseline
// and candidate values side-by-side makes OAAT (one-at-a-time) analysis clear.
struct TestedVariable {
  std::string name;
  std::string baseline_value;
  std::string candidate_value;
};

// Captures one experiment result row with both verdict and key metrics.
struct ResultRow {
  std::string experiment_id;
  std::string hypothesis_id;
  std::string variable_name;
  std::string variable_value;
  ResultStatus result = ResultStatus::kInconclusive;
  std::string evidence_run_id;
  double avg_fps = 0.0;
  double drop_rate_percent = 0.0;
  double jitter_p95_us = 0.0;
  std::string notes;
};

// Canonical in-memory state for agentic triage planning and progress tracking.
// This object is intentionally self-contained so it can be checkpointed between
// agent iterations and shipped inside engineer bundles.
struct ExperimentState {
  std::string session_id;
  std::string scenario_id;
  std::string baseline_id;
  std::uint64_t seed = 0;
  std::chrono::system_clock::time_point created_at{};
  std::chrono::system_clock::time_point updated_at{};
  std::vector<Hypothesis> hypotheses;
  std::vector<TestedVariable> tested_variables;
  std::vector<ResultRow> results_table;
  std::string next_action;
};

// JSON serializer for stable `agent_state.json` emission.
std::string ToJson(const ExperimentState& state);

} // namespace labops::agent
