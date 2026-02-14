#include "agent/engineer_packet_writer.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>

namespace fs = std::filesystem;

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

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  using labops::agent::EngineerPacketInput;
  using labops::agent::ExperimentState;
  using labops::agent::Hypothesis;
  using labops::agent::HypothesisStatus;
  using labops::agent::PacketConfigAttempt;
  using labops::agent::PacketRunEvidence;
  using labops::agent::ResultRow;
  using labops::agent::ResultStatus;
  using labops::agent::StopDecision;
  using labops::agent::StopReason;

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() /
                             ("labops-engineer-packet-smoke-" + std::to_string(now_ms));

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  ExperimentState state;
  state.session_id = "session-001";
  state.scenario_id = "trigger_roi";

  Hypothesis h1;
  h1.id = "h1";
  h1.statement = "ROI triggers timing instability";
  h1.variable_name = "roi_enabled";
  h1.status = HypothesisStatus::kSupported;
  state.hypotheses.push_back(h1);

  Hypothesis h2;
  h2.id = "h2";
  h2.statement = "Lower FPS alone causes failure";
  h2.variable_name = "fps";
  h2.status = HypothesisStatus::kRejected;
  state.hypotheses.push_back(h2);

  ResultRow row1;
  row1.hypothesis_id = "h1";
  row1.variable_name = "roi_enabled";
  row1.variable_value = "true";
  row1.result = ResultStatus::kFail;
  row1.evidence_run_id = "run-100";
  state.results_table.push_back(row1);

  ResultRow row2;
  row2.hypothesis_id = "h1";
  row2.variable_name = "roi_enabled";
  row2.variable_value = "false";
  row2.result = ResultStatus::kPass;
  row2.evidence_run_id = "run-101";
  state.results_table.push_back(row2);

  ResultRow row3;
  row3.hypothesis_id = "h2";
  row3.variable_name = "fps";
  row3.variable_value = "20";
  row3.result = ResultStatus::kPass;
  row3.evidence_run_id = "run-102";
  state.results_table.push_back(row3);

  PacketConfigAttempt attempt1;
  attempt1.sequence = 1;
  attempt1.run_id = "run-100";
  attempt1.knob_name = "roi_enabled";
  attempt1.knob_path = "camera.roi";
  attempt1.before_value = "false";
  attempt1.after_value = "true";
  attempt1.scenario_path = temp_root / "out" / "agent_runs" / "variant_roi.json";
  attempt1.result = ResultStatus::kFail;

  PacketConfigAttempt attempt2;
  attempt2.sequence = 2;
  attempt2.run_id = "run-102";
  attempt2.knob_name = "fps";
  attempt2.knob_path = "camera.fps";
  attempt2.before_value = "25";
  attempt2.after_value = "20";
  attempt2.scenario_path = temp_root / "out" / "agent_runs" / "variant_fps.json";
  attempt2.result = ResultStatus::kPass;

  PacketRunEvidence evidence1;
  evidence1.run_id = "run-100";
  evidence1.bundle_dir = temp_root / "out" / "runs" / "run-100";
  evidence1.run_json_path = evidence1.bundle_dir / "run.json";
  evidence1.events_jsonl_path = evidence1.bundle_dir / "events.jsonl";
  evidence1.metrics_json_path = evidence1.bundle_dir / "metrics.json";
  evidence1.summary_markdown_path = evidence1.bundle_dir / "summary.md";
  evidence1.diff_json_path = evidence1.bundle_dir / "diff.json";
  evidence1.diff_markdown_path = evidence1.bundle_dir / "diff.md";

  PacketRunEvidence evidence2;
  evidence2.run_id = "run-102";
  evidence2.bundle_dir = temp_root / "out" / "runs" / "run-102";
  evidence2.run_json_path = evidence2.bundle_dir / "run.json";
  evidence2.events_jsonl_path = evidence2.bundle_dir / "events.jsonl";
  evidence2.metrics_json_path = evidence2.bundle_dir / "metrics.json";
  evidence2.summary_markdown_path = evidence2.bundle_dir / "summary.md";
  evidence2.diff_json_path = evidence2.bundle_dir / "diff.json";
  evidence2.diff_markdown_path = evidence2.bundle_dir / "diff.md";

  StopDecision stop;
  stop.should_stop = true;
  stop.reason = StopReason::kSingleVariableFlip;
  stop.explanation = "stop: single-variable flip isolated roi_enabled";

  EngineerPacketInput input;
  input.state = &state;
  input.symptom = "dropped_frames";
  input.baseline_scenario_path = temp_root / "scenarios" / "sim_baseline.json";
  input.baseline_bundle_dir = temp_root / "baselines" / "sim_baseline";
  input.stop_decision = stop;
  input.configs_tried = {attempt1, attempt2};
  input.run_evidence = {evidence1, evidence2};

  fs::path written_path;
  std::string error;
  if (!labops::agent::WriteEngineerPacketMarkdown(input, temp_root / "packet", written_path, error)) {
    Fail("WriteEngineerPacketMarkdown failed: " + error);
  }

  const fs::path expected_path = temp_root / "packet" / "engineer_packet.md";
  if (written_path != expected_path) {
    Fail("engineer packet output path mismatch");
  }

  const std::string packet_text = ReadFile(written_path);

  AssertContains(packet_text, "## Repro Steps");
  AssertContains(packet_text, "## Configs Tried");
  AssertContains(packet_text, "## What Changed");
  AssertContains(packet_text, "## What We Ruled Out");
  AssertContains(packet_text, "## Ranked Hypotheses + Evidence Links");

  // Done-condition checks: packet must include exact artifact and diff paths.
  AssertContains(packet_text, evidence1.run_json_path.string());
  AssertContains(packet_text, evidence1.events_jsonl_path.string());
  AssertContains(packet_text, evidence1.diff_markdown_path.string());
  AssertContains(packet_text, evidence1.diff_json_path.string());
  AssertContains(packet_text, evidence2.diff_markdown_path.string());

  AssertContains(packet_text, input.baseline_scenario_path.string());
  AssertContains(packet_text, input.baseline_bundle_dir.string());
  AssertContains(packet_text, "single-variable flip");

  fs::remove_all(temp_root, ec);
  std::cout << "engineer_packet_writer_smoke: ok\n";
  return 0;
}
