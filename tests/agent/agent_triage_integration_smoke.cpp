#include "agent/engineer_packet_writer.hpp"
#include "agent/state_writer.hpp"
#include "agent/stop_conditions.hpp"
#include "agent/variant_generator.hpp"
#include "artifacts/metrics_diff_writer.hpp"
#include "core/errors/exit_codes.hpp"
#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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

void AssertFileExists(const fs::path& path) {
  if (!fs::exists(path) || !fs::is_regular_file(path)) {
    Fail("missing required file: " + path.string());
  }
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void WriteSeededKnownIssueScenario(const fs::path& scenario_path) {
  // This fixture is intentionally seeded and deterministic. The scenario is
  // healthy by default, but the OAAT `fps` mutation should fail the strict
  // min_avg_fps threshold and provide a reproducible isolation signal.
  std::ofstream scenario_file(scenario_path, std::ios::binary | std::ios::trunc);
  if (!scenario_file) {
    Fail("failed to open scenario file for writing: " + scenario_path.string());
  }

  scenario_file << "{\n"
                << "  \"schema_version\": \"1.0\",\n"
                << "  \"scenario_id\": \"agent_seeded_known_issue\",\n"
                << "  \"description\": \"Seeded triage fixture where OAAT fps mutation reproduces failure.\",\n"
                << "  \"tags\": [\"agent\", \"triage\", \"seeded\", \"integration\"],\n"
                << "  \"duration\": {\n"
                << "    \"duration_ms\": 600\n"
                << "  },\n"
                << "  \"camera\": {\n"
                << "    \"fps\": 30,\n"
                << "    \"pixel_format\": \"mono8\",\n"
                << "    \"trigger_mode\": \"free_run\"\n"
                << "  },\n"
                << "  \"sim_faults\": {\n"
                << "    \"seed\": 777,\n"
                << "    \"jitter_us\": 0,\n"
                << "    \"drop_every_n\": 0,\n"
                << "    \"drop_percent\": 0,\n"
                << "    \"burst_drop\": 0,\n"
                << "    \"reorder\": 0\n"
                << "  },\n"
                << "  \"thresholds\": {\n"
                << "    \"min_avg_fps\": 29.5,\n"
                << "    \"max_drop_rate_percent\": 100.0\n"
                << "  }\n"
                << "}\n";
  if (!scenario_file) {
    Fail("failed while writing seeded known-issue scenario");
  }
}

labops::agent::ResultStatus ToResultStatus(int exit_code) {
  if (exit_code == labops::core::errors::ToInt(labops::core::errors::ExitCode::kSuccess)) {
    return labops::agent::ResultStatus::kPass;
  }
  if (exit_code ==
      labops::core::errors::ToInt(labops::core::errors::ExitCode::kThresholdsFailed)) {
    return labops::agent::ResultStatus::kFail;
  }
  return labops::agent::ResultStatus::kInconclusive;
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-agent-triage-integration-" + std::to_string(now_ms));
  const fs::path scenario_dir = temp_root / "scenarios";
  const fs::path out_root = temp_root / "out";
  const fs::path base_scenario_path = scenario_dir / "seeded_known_issue.json";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(scenario_dir, ec);
  if (ec) {
    Fail("failed to create temp scenario directory");
  }
  WriteSeededKnownIssueScenario(base_scenario_path);

  // 1) Generate OAAT variants for the dropped-frames symptom.
  labops::agent::VariantGenerationRequest generation_request;
  generation_request.base_scenario_path = base_scenario_path.string();
  generation_request.symptom = "dropped_frames";
  generation_request.output_dir = out_root / "agent_runs";

  labops::agent::VariantGenerationResult variants;
  std::string error;
  labops::agent::OaatVariantGenerator generator;
  if (!generator.Generate(generation_request, variants, error)) {
    Fail("failed to generate OAAT variants: " + error);
  }
  if (variants.variants.empty()) {
    Fail("expected at least one generated OAAT variant");
  }
  AssertFileExists(variants.manifest_path);

  // 2) Run baseline once (expected PASS) so every variant can be compared
  // against known-good reference metrics.
  labops::cli::RunOptions baseline_options;
  baseline_options.scenario_path = base_scenario_path.string();
  baseline_options.output_dir = out_root / "baselines" / "seeded_known_issue";
  baseline_options.zip_bundle = false;

  labops::cli::ScenarioRunResult baseline_run;
  const int baseline_exit = labops::cli::ExecuteScenarioRun(
      baseline_options,
      /*use_per_run_bundle_dir=*/false,
      /*allow_zip_bundle=*/false,
      "agent triage baseline captured: ",
      &baseline_run);
  if (baseline_exit != 0) {
    Fail("baseline run should pass, got exit code " + std::to_string(baseline_exit));
  }
  AssertFileExists(baseline_run.bundle_dir / "metrics.csv");

  // 3) Execute every variant, compare to baseline, and accumulate evidence for
  // stop-condition evaluation + engineer packet generation.
  std::vector<labops::agent::PacketConfigAttempt> attempts;
  std::vector<labops::agent::PacketRunEvidence> run_evidence;
  std::vector<labops::agent::ResultRow> result_rows;
  attempts.reserve(variants.variants.size());
  run_evidence.reserve(variants.variants.size() + 1U);
  result_rows.reserve(variants.variants.size() + 1U);

  // Include baseline evidence so the packet has a full repro chain.
  labops::agent::PacketRunEvidence baseline_evidence;
  baseline_evidence.run_id = baseline_run.run_id;
  baseline_evidence.bundle_dir = baseline_run.bundle_dir;
  baseline_evidence.run_json_path = baseline_run.run_json_path;
  baseline_evidence.events_jsonl_path = baseline_run.events_jsonl_path;
  baseline_evidence.metrics_json_path = baseline_run.metrics_json_path;
  baseline_evidence.summary_markdown_path = baseline_run.bundle_dir / "summary.md";
  run_evidence.push_back(std::move(baseline_evidence));

  bool saw_fps_variant = false;
  bool saw_fps_failure = false;
  std::string fps_before_value;
  std::string fps_after_value;

  std::size_t sequence = 0;
  for (const auto& variant : variants.variants) {
    ++sequence;

    labops::cli::RunOptions variant_options;
    variant_options.scenario_path = variant.scenario_path.string();
    variant_options.output_dir = out_root / "runs";
    variant_options.zip_bundle = false;

    labops::cli::ScenarioRunResult variant_run;
    const int variant_exit = labops::cli::ExecuteScenarioRun(
        variant_options,
        /*use_per_run_bundle_dir=*/true,
        /*allow_zip_bundle=*/true,
        "agent triage variant queued: ",
        &variant_run);

    if (variant_exit != 0 &&
        variant_exit !=
            labops::core::errors::ToInt(labops::core::errors::ExitCode::kThresholdsFailed)) {
      Fail("variant run failed unexpectedly with exit code " + std::to_string(variant_exit));
    }

    const fs::path variant_metrics_csv = variant_run.bundle_dir / "metrics.csv";
    AssertFileExists(variant_metrics_csv);

    labops::artifacts::MetricsDiffReport diff_report;
    if (!labops::artifacts::ComputeMetricsDiffFromCsv(baseline_run.bundle_dir / "metrics.csv",
                                                      variant_metrics_csv,
                                                      diff_report,
                                                      error)) {
      Fail("failed to compute variant diff: " + error);
    }

    fs::path diff_json_path;
    if (!labops::artifacts::WriteMetricsDiffJson(diff_report,
                                                 variant_run.bundle_dir,
                                                 diff_json_path,
                                                 error)) {
      Fail("failed to write diff.json: " + error);
    }

    fs::path diff_markdown_path;
    if (!labops::artifacts::WriteMetricsDiffMarkdown(diff_report,
                                                     variant_run.bundle_dir,
                                                     diff_markdown_path,
                                                     error)) {
      Fail("failed to write diff.md: " + error);
    }

    labops::agent::PacketConfigAttempt attempt;
    attempt.sequence = sequence;
    attempt.run_id = variant_run.run_id;
    attempt.knob_name = variant.knob_name;
    attempt.knob_path = variant.knob_path;
    attempt.before_value = variant.before_value;
    attempt.after_value = variant.after_value;
    attempt.scenario_path = variant.scenario_path;
    attempt.result = ToResultStatus(variant_exit);
    attempt.notes = "triage integration OAAT run";
    attempts.push_back(attempt);

    labops::agent::PacketRunEvidence evidence;
    evidence.run_id = variant_run.run_id;
    evidence.bundle_dir = variant_run.bundle_dir;
    evidence.run_json_path = variant_run.run_json_path;
    evidence.events_jsonl_path = variant_run.events_jsonl_path;
    evidence.metrics_json_path = variant_run.metrics_json_path;
    evidence.summary_markdown_path = variant_run.bundle_dir / "summary.md";
    evidence.diff_json_path = diff_json_path;
    evidence.diff_markdown_path = diff_markdown_path;
    run_evidence.push_back(std::move(evidence));

    labops::agent::ResultRow row;
    row.experiment_id = "exp-" + std::to_string(sequence);
    row.hypothesis_id = "h_fps_threshold";
    row.variable_name = variant.knob_path;
    row.variable_value = variant.after_value;
    row.result = ToResultStatus(variant_exit);
    row.evidence_run_id = variant_run.run_id;
    row.notes = "variant knob=" + variant.knob_name;
    result_rows.push_back(std::move(row));

    if (variant.knob_name == "fps") {
      saw_fps_variant = true;
      fps_before_value = variant.before_value;
      fps_after_value = variant.after_value;
      saw_fps_failure = (variant_exit ==
                         labops::core::errors::ToInt(
                             labops::core::errors::ExitCode::kThresholdsFailed));
    }
  }

  if (!saw_fps_variant) {
    Fail("expected dropped_frames playbook to include fps knob");
  }
  if (!saw_fps_failure) {
    Fail("expected seeded known issue to fail when fps knob is mutated");
  }

  // 4) Build agent state + stop decision. We add one explicit baseline pass
  // row for the same variable so stop logic can isolate a single-variable flip.
  labops::agent::ExperimentState state;
  state.session_id = "session-" + std::to_string(now_ms);
  state.scenario_id = "agent_seeded_known_issue";
  state.baseline_id = baseline_run.run_id;
  state.seed = 777;
  state.created_at = std::chrono::system_clock::now();
  state.updated_at = state.created_at;
  state.next_action = "emit engineer packet";

  labops::agent::Hypothesis hypothesis;
  hypothesis.id = "h_fps_threshold";
  hypothesis.statement = "fps reduction below threshold reproduces the failure.";
  hypothesis.variable_name = "camera.fps";
  hypothesis.status = labops::agent::HypothesisStatus::kSupported;
  state.hypotheses.push_back(hypothesis);

  for (const auto& attempt : attempts) {
    labops::agent::TestedVariable tested;
    tested.name = attempt.knob_path;
    tested.baseline_value = attempt.before_value;
    tested.candidate_value = attempt.after_value;
    state.tested_variables.push_back(std::move(tested));
  }

  labops::agent::ResultRow baseline_row;
  baseline_row.experiment_id = "exp-baseline";
  baseline_row.hypothesis_id = "h_fps_threshold";
  baseline_row.variable_name = "camera.fps";
  baseline_row.variable_value = fps_before_value;
  baseline_row.result = labops::agent::ResultStatus::kPass;
  baseline_row.evidence_run_id = baseline_run.run_id;
  baseline_row.notes = "baseline known-good value";
  state.results_table.push_back(std::move(baseline_row));
  state.results_table.insert(state.results_table.end(), result_rows.begin(), result_rows.end());

  labops::agent::StopConfig stop_config;
  stop_config.max_runs = 20;
  stop_config.stable_repro_window = 4;
  stop_config.stable_repro_rate_min = 0.95;
  stop_config.confidence_threshold = 0.95;

  labops::agent::StopInput stop_input;
  stop_input.state = &state;
  stop_input.confidence_score = 0.25;

  labops::agent::StopDecision stop_decision;
  if (!labops::agent::EvaluateStopConditions(stop_config, stop_input, stop_decision, error)) {
    Fail("failed to evaluate stop conditions: " + error);
  }
  if (!stop_decision.should_stop) {
    Fail("expected triage stop condition to trigger");
  }
  if (stop_decision.reason != labops::agent::StopReason::kSingleVariableFlip) {
    Fail("expected stop reason to be single-variable flip");
  }
  if (stop_decision.isolating_variable != "camera.fps") {
    Fail("expected isolating variable camera.fps");
  }
  AssertContains(stop_decision.explanation, "single-variable flip");
  AssertContains(stop_decision.explanation, fps_after_value);

  // 5) Persist agent state and engineer packet for handoff.
  fs::path state_path;
  if (!labops::agent::WriteAgentStateJson(state, out_root / "agent", state_path, error)) {
    Fail("failed to write agent_state.json: " + error);
  }
  AssertFileExists(state_path);

  labops::agent::EngineerPacketInput packet_input;
  packet_input.state = &state;
  packet_input.symptom = "dropped_frames";
  packet_input.baseline_scenario_path = base_scenario_path;
  packet_input.baseline_bundle_dir = baseline_run.bundle_dir;
  packet_input.stop_decision = stop_decision;
  packet_input.configs_tried = attempts;
  packet_input.run_evidence = run_evidence;

  fs::path packet_path;
  if (!labops::agent::WriteEngineerPacketMarkdown(packet_input, out_root / "packet", packet_path,
                                                  error)) {
    Fail("failed to write engineer packet: " + error);
  }
  AssertFileExists(packet_path);

  const std::string packet_text = ReadFile(packet_path);
  AssertContains(packet_text, "# Engineer Packet");
  AssertContains(packet_text, "single_variable_flip");
  AssertContains(packet_text, "camera.fps");
  AssertContains(packet_text, baseline_run.bundle_dir.string());

  fs::remove_all(temp_root, ec);
  std::cout << "agent_triage_integration_smoke: ok\n";
  return 0;
}
