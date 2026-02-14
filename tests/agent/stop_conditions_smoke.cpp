#include "agent/stop_conditions.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

labops::agent::ResultRow MakeRow(std::string variable_name, std::string variable_value,
                                 labops::agent::ResultStatus result) {
  labops::agent::ResultRow row;
  row.variable_name = std::move(variable_name);
  row.variable_value = std::move(variable_value);
  row.result = result;
  return row;
}

} // namespace

int main() {
  using labops::agent::EvaluateStopConditions;
  using labops::agent::ExperimentState;
  using labops::agent::ResultStatus;
  using labops::agent::StopConfig;
  using labops::agent::StopDecision;
  using labops::agent::StopInput;
  using labops::agent::StopReason;
  using labops::agent::ToString;

  {
    // Deterministic priority check: max_runs must win when multiple conditions
    // are simultaneously true.
    ExperimentState state;
    state.results_table.push_back(MakeRow("roi_enabled", "true", ResultStatus::kFail));
    state.results_table.push_back(MakeRow("roi_enabled", "false", ResultStatus::kPass));
    state.results_table.push_back(MakeRow("roi_enabled", "true", ResultStatus::kFail));
    state.results_table.push_back(MakeRow("roi_enabled", "false", ResultStatus::kPass));

    StopConfig config;
    config.max_runs = 4;
    config.stable_repro_window = 4;
    config.stable_repro_rate_min = 0.5;
    config.confidence_threshold = 0.10;

    StopInput input;
    input.state = &state;
    input.confidence_score = 0.95;

    StopDecision decision;
    std::string error;
    if (!EvaluateStopConditions(config, input, decision, error)) {
      Fail("EvaluateStopConditions failed unexpectedly");
    }
    if (!decision.should_stop || decision.reason != StopReason::kMaxRuns) {
      Fail("expected max-runs stop reason to win deterministic priority");
    }
    AssertContains(decision.explanation, "reached max runs");
  }

  {
    ExperimentState state;
    state.results_table.push_back(MakeRow("trigger_mode", "hardware", ResultStatus::kFail));
    state.results_table.push_back(MakeRow("trigger_mode", "free_run", ResultStatus::kPass));

    StopConfig config;
    config.max_runs = 20;

    StopInput input;
    input.state = &state;
    input.confidence_score = 0.20;

    StopDecision decision;
    std::string error;
    if (!EvaluateStopConditions(config, input, decision, error)) {
      Fail("single-variable-flip evaluation should succeed");
    }
    if (decision.reason != StopReason::kSingleVariableFlip) {
      Fail("expected single-variable flip stop reason");
    }
    AssertContains(decision.explanation, "single-variable flip");
    AssertContains(decision.explanation, "trigger_mode");
    if (decision.isolating_variable != "trigger_mode") {
      Fail("isolating variable should be populated");
    }
  }

  {
    ExperimentState state;
    state.results_table.push_back(MakeRow("fps", "30", ResultStatus::kFail));

    StopConfig config;
    config.max_runs = 20;
    config.confidence_threshold = 0.80;

    StopInput input;
    input.state = &state;
    input.confidence_score = 0.85;

    StopDecision decision;
    std::string error;
    if (!EvaluateStopConditions(config, input, decision, error)) {
      Fail("confidence-threshold evaluation should succeed");
    }
    if (decision.reason != StopReason::kConfidenceThreshold) {
      Fail("expected confidence-threshold stop reason");
    }
    AssertContains(decision.explanation, "confidence score");
  }

  {
    ExperimentState state;
    state.results_table.push_back(MakeRow("fps", "25", ResultStatus::kFail));
    state.results_table.push_back(MakeRow("fps", "25", ResultStatus::kFail));
    state.results_table.push_back(MakeRow("fps", "25", ResultStatus::kFail));
    state.results_table.push_back(MakeRow("fps", "25", ResultStatus::kPass));

    StopConfig config;
    config.max_runs = 20;
    config.stable_repro_window = 4;
    config.stable_repro_rate_min = 0.75;
    config.confidence_threshold = 0.95;

    StopInput input;
    input.state = &state;
    input.confidence_score = 0.10;

    StopDecision decision_a;
    StopDecision decision_b;
    std::string error;
    if (!EvaluateStopConditions(config, input, decision_a, error) ||
        !EvaluateStopConditions(config, input, decision_b, error)) {
      Fail("stable-repro-rate evaluation should succeed");
    }
    if (decision_a.reason != StopReason::kStableReproRate) {
      Fail("expected stable-repro-rate stop reason");
    }

    // Determinism contract: same input/state should yield same reason and text.
    if (decision_a.reason != decision_b.reason ||
        decision_a.explanation != decision_b.explanation) {
      Fail("stop-condition evaluation should be deterministic");
    }
    AssertContains(decision_a.explanation, "stable repro rate");
  }

  {
    ExperimentState state;
    state.results_table.push_back(MakeRow("", "", ResultStatus::kInconclusive));

    StopConfig config;
    config.max_runs = 10;
    config.stable_repro_window = 3;
    config.stable_repro_rate_min = 0.8;
    config.confidence_threshold = 0.9;

    StopInput input;
    input.state = &state;
    input.confidence_score = 0.4;

    StopDecision decision;
    std::string error;
    if (!EvaluateStopConditions(config, input, decision, error)) {
      Fail("continue evaluation should succeed");
    }
    if (decision.should_stop || decision.reason != StopReason::kContinue) {
      Fail("expected continue decision when no stop condition is met");
    }
    AssertContains(decision.explanation, "no stop condition met");
  }

  {
    StopConfig config;
    StopInput input;
    input.state = nullptr;
    input.confidence_score = 0.5;

    StopDecision decision;
    std::string error;
    if (EvaluateStopConditions(config, input, decision, error)) {
      Fail("expected null-state validation failure");
    }
    AssertContains(error, "state cannot be null");
  }

  if (std::string(ToString(StopReason::kSingleVariableFlip)) != "single_variable_flip") {
    Fail("StopReason string contract mismatch");
  }

  std::cout << "stop_conditions_smoke: ok\n";
  return 0;
}
