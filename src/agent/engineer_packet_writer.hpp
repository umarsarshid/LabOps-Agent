#pragma once

#include "agent/experiment_state.hpp"
#include "agent/stop_conditions.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace labops::agent {

// Canonical artifact links for one executed run. These paths are copied into
// the engineer packet exactly so humans can jump straight to evidence.
struct PacketRunEvidence {
  std::string run_id;
  std::filesystem::path bundle_dir;
  std::filesystem::path run_json_path;
  std::filesystem::path events_jsonl_path;
  std::filesystem::path metrics_json_path;
  std::filesystem::path summary_markdown_path;
  std::filesystem::path diff_json_path;
  std::filesystem::path diff_markdown_path;
};

// One configuration mutation attempt in OAAT order.
struct PacketConfigAttempt {
  std::size_t sequence = 0;
  std::string run_id;
  std::string knob_name;
  std::string knob_path;
  std::string before_value;
  std::string after_value;
  std::filesystem::path scenario_path;
  ResultStatus result = ResultStatus::kInconclusive;
  std::string notes;
};

// Inputs needed to generate a complete engineer handoff packet.
struct EngineerPacketInput {
  const ExperimentState* state = nullptr;
  std::string symptom;
  std::filesystem::path baseline_scenario_path;
  std::filesystem::path baseline_bundle_dir;
  StopDecision stop_decision;
  std::vector<PacketConfigAttempt> configs_tried;
  std::vector<PacketRunEvidence> run_evidence;
};

// Writes `engineer_packet.md` with reproducible handoff details:
// - repro steps
// - configs tried / what changed
// - ruled-out paths
// - ranked hypotheses with evidence links
//
// Contract:
// - creates `output_dir` as needed
// - writes `<output_dir>/engineer_packet.md`
// - returns false with actionable `error` on invalid input or I/O failure
bool WriteEngineerPacketMarkdown(const EngineerPacketInput& input,
                                 const std::filesystem::path& output_dir,
                                 std::filesystem::path& written_path,
                                 std::string& error);

} // namespace labops::agent
