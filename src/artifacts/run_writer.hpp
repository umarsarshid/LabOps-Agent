#pragma once

#include "core/schema/run_contract.hpp"

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Emits the canonical `run.json` artifact for a run.
//
// Contract:
// - Creates `output_dir` if needed.
// - Writes UTF-8 JSON to `<output_dir>/run.json`.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteRunJson(const core::schema::RunInfo& run_info, const std::filesystem::path& output_dir,
                  std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
