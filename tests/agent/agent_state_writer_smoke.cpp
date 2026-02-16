#include "../common/assertions.hpp"
#include "../common/temp_dir.hpp"

#include "agent/experiment_state.hpp"
#include "agent/state_writer.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::AssertContains;
using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::Fail;
using labops::tests::common::ReadFileToString;

} // namespace

int main() {
  using labops::agent::ExperimentState;
  using labops::agent::Hypothesis;
  using labops::agent::HypothesisStatus;
  using labops::agent::ResultRow;
  using labops::agent::ResultStatus;
  using labops::agent::TestedVariable;

  const auto fixed_time =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'555'000));

  ExperimentState state;
  state.session_id = "agent-session-01";
  state.scenario_id = "trigger_roi";
  state.baseline_id = "sim_baseline";
  state.seed = 777;
  state.created_at = fixed_time;
  state.updated_at = fixed_time;
  state.next_action = "toggle ROI off and rerun";

  Hypothesis hypothesis;
  hypothesis.id = "h1";
  hypothesis.statement = "ROI mode triggers timing stalls";
  hypothesis.variable_name = "roi_enabled";
  hypothesis.status = HypothesisStatus::kOpen;
  state.hypotheses.push_back(hypothesis);

  TestedVariable tested;
  tested.name = "roi_enabled";
  tested.baseline_value = "false";
  tested.candidate_value = "true";
  state.tested_variables.push_back(tested);

  ResultRow row;
  row.experiment_id = "exp-001";
  row.hypothesis_id = "h1";
  row.variable_name = "roi_enabled";
  row.variable_value = "true";
  row.result = ResultStatus::kFail;
  row.evidence_run_id = "run-1700000555000";
  row.avg_fps = 22.75;
  row.drop_rate_percent = 18.0;
  row.jitter_p95_us = 4500.0;
  row.notes = "Drop spike appears after ROI enable.";
  state.results_table.push_back(row);

  const fs::path temp_root =
      CreateUniqueTempDir("labops-agent-state-writer-smoke-agent-session-01");
  const fs::path out_dir = temp_root / "out";
  std::error_code cleanup_ec;

  fs::path written_path;
  std::string error;
  if (!labops::agent::WriteAgentStateJson(state, out_dir, written_path, error)) {
    Fail("WriteAgentStateJson failed: " + error);
  }

  const fs::path expected_path = out_dir / "agent_state.json";
  if (written_path != expected_path) {
    Fail("written path mismatch");
  }

  std::string content = ReadFileToString(written_path);
  AssertContains(content, "\"session_id\":\"agent-session-01\"");
  AssertContains(content, "\"scenario_id\":\"trigger_roi\"");
  AssertContains(content, "\"baseline_id\":\"sim_baseline\"");
  AssertContains(content, "\"hypotheses\":[");
  AssertContains(content, "\"tested_variables\":[");
  AssertContains(content, "\"results_table\":[");
  AssertContains(content, "\"result\":\"fail\"");
  AssertContains(content, "\"avg_fps\":22.750");
  AssertContains(content, "\"drop_rate_percent\":18.000");
  AssertContains(content, "\"jitter_p95_us\":4500.000");

  fs::remove_all(temp_root, cleanup_ec);
  std::cout << "agent_state_writer_smoke: ok\n";
  return 0;
}
