#pragma once

#include "metrics/fps.hpp"

#include <filesystem>
#include <string>

namespace labops::metrics {

// Emits `metrics.csv` for run performance metrics.
//
// Contract:
// - Creates `output_dir` if needed.
// - Writes UTF-8 CSV to `<output_dir>/metrics.csv`.
// - Includes one `avg_fps` summary row and zero or more `rolling_fps` rows.
// - Includes inter-frame interval/jitter min+avg+p95 summary rows.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteFpsMetricsCsv(const FpsReport& report, const std::filesystem::path& output_dir,
                        std::filesystem::path& written_path, std::string& error);

} // namespace labops::metrics
