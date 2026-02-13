#pragma once

#include "metrics/fps.hpp"

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Emits the canonical `metrics.csv` artifact for a run.
//
// Contract:
// - Creates `output_dir` if needed.
// - Writes UTF-8 CSV to `<output_dir>/metrics.csv`.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteMetricsCsv(const metrics::FpsReport& report, const std::filesystem::path& output_dir,
                     std::filesystem::path& written_path, std::string& error);

// Emits the canonical `metrics.json` artifact for a run.
//
// Contract:
// - Creates `output_dir` if needed.
// - Writes UTF-8 JSON to `<output_dir>/metrics.json`.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteMetricsJson(const metrics::FpsReport& report, const std::filesystem::path& output_dir,
                      std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
