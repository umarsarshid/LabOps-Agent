#pragma once

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Copies the source scenario JSON into the run bundle as `scenario.json`.
//
// Contract:
// - Creates `output_dir` if needed.
// - Writes UTF-8 bytes to `<output_dir>/scenario.json`.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteScenarioJson(const std::filesystem::path& source_scenario_path,
                       const std::filesystem::path& output_dir, std::filesystem::path& written_path,
                       std::string& error);

} // namespace labops::artifacts
