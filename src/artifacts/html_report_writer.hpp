#pragma once

#include "core/schema/run_contract.hpp"
#include "metrics/fps.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace labops::artifacts {

// Writes a static HTML summary artifact (`report.html`) for a completed run.
//
// Why this exists:
// - `summary.md` is optimized for quick human scanning in terminals.
// - `report.html` is optimized for browser viewing and copy/paste into docs
//   while staying fully static (no JS/runtime dependencies).
// - table layout keeps values "plots-ready" for manual charting tools.
//
// Contract:
// - creates `output_dir` when missing.
// - writes `<output_dir>/report.html`.
// - includes key metrics and explicit deltas vs expected targets.
// - returns false and sets `error` on failure.
bool WriteRunSummaryHtml(const core::schema::RunInfo& run_info, const metrics::FpsReport& report,
                         std::uint32_t configured_fps, bool thresholds_passed,
                         const std::vector<std::string>& threshold_failures,
                         const std::vector<std::string>& top_anomalies,
                         const std::filesystem::path& output_dir,
                         std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
