#include "agent/stop_conditions.hpp"

#include "core/time_utils.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace labops::agent {

namespace {

bool IsDecisive(ResultStatus status) {
  return status == ResultStatus::kPass || status == ResultStatus::kFail;
}

bool IsRepro(ResultStatus status) {
  return status == ResultStatus::kFail;
}

struct ValueOutcomeStats {
  std::size_t pass_count = 0;
  std::size_t fail_count = 0;
};

struct FlipEvidence {
  std::string variable;
  std::string fail_value;
  std::string pass_value;
};

std::optional<FlipEvidence> FindSingleVariableFlip(const ExperimentState& state) {
  // Sorted maps keep iteration deterministic across platforms.
  std::map<std::string, std::map<std::string, ValueOutcomeStats>> stats;

  for (const auto& row : state.results_table) {
    if (!IsDecisive(row.result) || row.variable_name.empty()) {
      continue;
    }

    auto& value_stats = stats[row.variable_name][row.variable_value];
    if (row.result == ResultStatus::kPass) {
      ++value_stats.pass_count;
    } else if (row.result == ResultStatus::kFail) {
      ++value_stats.fail_count;
    }
  }

  for (const auto& [variable_name, value_table] : stats) {
    std::optional<std::string> fail_value;
    std::optional<std::string> pass_value;

    for (const auto& [value, value_stats] : value_table) {
      if (!fail_value.has_value() && value_stats.fail_count > 0U) {
        fail_value = value;
      }
      if (!pass_value.has_value() && value_stats.pass_count > 0U) {
        pass_value = value;
      }
    }

    // Require evidence across at least two values so we do not claim a flip
    // from contradictory outcomes of one value.
    if (fail_value.has_value() && pass_value.has_value() &&
        fail_value.value() != pass_value.value()) {
      FlipEvidence evidence;
      evidence.variable = variable_name;
      evidence.fail_value = fail_value.value();
      evidence.pass_value = pass_value.value();
      return evidence;
    }
  }

  return std::nullopt;
}

bool ValidateConfig(const StopConfig& config, std::string& error) {
  if (config.max_runs == 0U) {
    error = "max_runs must be greater than 0";
    return false;
  }
  if (config.stable_repro_window == 0U) {
    error = "stable_repro_window must be greater than 0";
    return false;
  }
  if (!std::isfinite(config.stable_repro_rate_min) || config.stable_repro_rate_min < 0.0 ||
      config.stable_repro_rate_min > 1.0) {
    error = "stable_repro_rate_min must be in [0,1]";
    return false;
  }
  if (!std::isfinite(config.confidence_threshold) || config.confidence_threshold < 0.0 ||
      config.confidence_threshold > 1.0) {
    error = "confidence_threshold must be in [0,1]";
    return false;
  }
  return true;
}

bool ValidateInput(const StopInput& input, std::string& error) {
  if (input.state == nullptr) {
    error = "stop input state cannot be null";
    return false;
  }
  if (!std::isfinite(input.confidence_score) || input.confidence_score < 0.0 ||
      input.confidence_score > 1.0) {
    error = "confidence_score must be in [0,1]";
    return false;
  }
  return true;
}

} // namespace

const char* ToString(StopReason reason) {
  switch (reason) {
  case StopReason::kContinue:
    return "continue";
  case StopReason::kMaxRuns:
    return "max_runs";
  case StopReason::kSingleVariableFlip:
    return "single_variable_flip";
  case StopReason::kConfidenceThreshold:
    return "confidence_threshold";
  case StopReason::kStableReproRate:
    return "stable_repro_rate";
  }

  return "continue";
}

bool EvaluateStopConditions(const StopConfig& config, const StopInput& input,
                            StopDecision& decision, std::string& error) {
  decision = StopDecision{};
  error.clear();

  if (!ValidateConfig(config, error) || !ValidateInput(input, error)) {
    return false;
  }

  const ExperimentState& state = *input.state;
  decision.run_count = state.results_table.size();
  decision.observed_confidence = input.confidence_score;

  std::vector<ResultStatus> decisive;
  decisive.reserve(state.results_table.size());
  for (const auto& row : state.results_table) {
    if (IsDecisive(row.result)) {
      decisive.push_back(row.result);
    }
  }
  decision.decisive_run_count = decisive.size();

  if (!decisive.empty()) {
    const std::size_t window = std::min(config.stable_repro_window, decisive.size());
    decision.repro_window_count = window;

    std::size_t repro_count = 0;
    const auto start = decisive.end() - static_cast<std::ptrdiff_t>(window);
    for (auto it = start; it != decisive.end(); ++it) {
      if (IsRepro(*it)) {
        ++repro_count;
      }
    }
    decision.observed_repro_rate = static_cast<double>(repro_count) / static_cast<double>(window);
  }

  // Priority 1: hard safety cap so automation cannot run unbounded.
  if (decision.run_count >= config.max_runs) {
    decision.should_stop = true;
    decision.reason = StopReason::kMaxRuns;
    decision.explanation =
        "stop: reached max runs (run_count=" + std::to_string(decision.run_count) +
        ", max_runs=" + std::to_string(config.max_runs) + ")";
    return true;
  }

  // Priority 2: strongest isolation signal, one variable with explicit pass/fail flip.
  if (const auto flip = FindSingleVariableFlip(state); flip.has_value()) {
    decision.should_stop = true;
    decision.reason = StopReason::kSingleVariableFlip;
    decision.isolating_variable = flip->variable;
    decision.explanation = "stop: single-variable flip isolated variable '" + flip->variable +
                           "' (value='" + flip->fail_value + "' => fail, value='" +
                           flip->pass_value + "' => pass)";
    return true;
  }

  // Priority 3: caller-provided confidence signal crosses explicit threshold.
  if (input.confidence_score >= config.confidence_threshold) {
    decision.should_stop = true;
    decision.reason = StopReason::kConfidenceThreshold;
    decision.explanation =
        "stop: confidence score " + core::FormatFixedDouble(input.confidence_score, 3) +
        " reached threshold " + core::FormatFixedDouble(config.confidence_threshold, 3);
    return true;
  }

  // Priority 4: reproducibility stabilized over required recent decision window.
  if (decision.repro_window_count == config.stable_repro_window &&
      decision.observed_repro_rate >= config.stable_repro_rate_min) {
    decision.should_stop = true;
    decision.reason = StopReason::kStableReproRate;
    decision.explanation = "stop: stable repro rate " +
                           core::FormatFixedDouble(decision.observed_repro_rate, 3) +
                           " over last " + std::to_string(decision.repro_window_count) +
                           " decisive runs reached threshold " +
                           core::FormatFixedDouble(config.stable_repro_rate_min, 3);
    return true;
  }

  decision.should_stop = false;
  decision.reason = StopReason::kContinue;
  decision.explanation =
      "continue: no stop condition met (run_count=" + std::to_string(decision.run_count) +
      ", confidence=" + core::FormatFixedDouble(input.confidence_score, 3) +
      ", recent_repro_rate=" + core::FormatFixedDouble(decision.observed_repro_rate, 3) +
      ", repro_window=" + std::to_string(decision.repro_window_count) + ")";
  return true;
}

} // namespace labops::agent
