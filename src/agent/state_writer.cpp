#include "agent/state_writer.hpp"

#include "core/fs_utils.hpp"

namespace fs = std::filesystem;

namespace labops::agent {

bool WriteAgentStateJson(const ExperimentState& state, const fs::path& output_dir,
                         fs::path& written_path, std::string& error) {
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  written_path = output_dir / "agent_state.json";
  // Keep newline termination so shell inspection (`cat`, `tail`) is clean.
  std::string json = ToJson(state);
  json.push_back('\n');
  if (!core::WriteTextFileAtomic(written_path, json, error)) {
    error = "failed while writing output file '" + written_path.string() + "' (" + error + ")";
    return false;
  }

  return true;
}

} // namespace labops::agent
