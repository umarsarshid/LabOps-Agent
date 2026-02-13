#include "artifacts/scenario_writer.hpp"

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

  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }

  std::ifstream in_file(source_scenario_path, std::ios::binary);
  if (!in_file) {
    error = "failed to open source scenario file '" + source_scenario_path.string() + "'";
    return false;
  }

  written_path = output_dir / "scenario.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output scenario file '" + written_path.string() + "'";
    return false;
  }

  out_file << in_file.rdbuf();
  if (!out_file) {
    error = "failed while writing scenario file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
