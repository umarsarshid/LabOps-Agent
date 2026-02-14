#include "agent/state_writer.hpp"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::agent {

bool WriteAgentStateJson(const ExperimentState& state, const fs::path& output_dir,
                         fs::path& written_path, std::string& error) {
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }

  written_path = output_dir / "agent_state.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  // Keep newline termination so shell inspection (`cat`, `tail`) is clean.
  out_file << ToJson(state) << '\n';
  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::agent
