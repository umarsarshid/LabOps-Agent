#pragma once

#include "agent/experiment_state.hpp"

#include <filesystem>
#include <string>

namespace labops::agent {

// Writes the checkpoint artifact for agent planning progress.
//
// Contract:
// - creates `output_dir` if missing
// - writes `<output_dir>/agent_state.json`
// - returns written path on success
bool WriteAgentStateJson(const ExperimentState& state, const std::filesystem::path& output_dir,
                         std::filesystem::path& written_path, std::string& error);

} // namespace labops::agent
