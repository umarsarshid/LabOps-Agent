#include "artifacts/scenario_writer.hpp"

#include "core/fs_utils.hpp"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

bool WriteScenarioJson(const fs::path& source_scenario_path, const fs::path& output_dir,
                       fs::path& written_path, std::string& error) {
  if (source_scenario_path.empty()) {
    error = "source scenario path cannot be empty";
    return false;
  }
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  std::error_code ec;
  if (!fs::exists(source_scenario_path, ec) || ec) {
    error = "source scenario file not found: " + source_scenario_path.string();
    return false;
  }
  if (!fs::is_regular_file(source_scenario_path, ec) || ec) {
    error = "source scenario path must be a regular file: " + source_scenario_path.string();
    return false;
  }

  std::ifstream in_file(source_scenario_path, std::ios::binary);
  if (!in_file) {
    error = "failed to open source scenario file '" + source_scenario_path.string() + "'";
    return false;
  }

  written_path = output_dir / "scenario.json";
  const std::string scenario_text((std::istreambuf_iterator<char>(in_file)),
                                  std::istreambuf_iterator<char>());
  if (!core::WriteTextFileAtomic(written_path, scenario_text, error)) {
    error = "failed while writing scenario file '" + written_path.string() + "' (" + error + ")";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
