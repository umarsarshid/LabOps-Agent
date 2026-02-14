#pragma once

#include "core/schema/run_contract.hpp"
#include "metrics/fps.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace labops::artifacts {

// Writes a one-page human-readable run summary (`summary.md`).
//
// Contract:
// - creates `output_dir` when missing.
// - writes `<output_dir>/summary.md`.
// - includes key metrics, threshold pass/fail, and top anomalies.
// - returns false and sets `error` on failure.
bool WriteRunSummaryMarkdown(const core::schema::RunInfo& run_info,
                             const metrics::FpsReport& report,
                             std::uint32_t configured_fps,
                             bool thresholds_passed,
                             const std::vector<std::string>& threshold_failures,
                             const std::vector<std::string>& top_anomalies,
                             const std::filesystem::path& output_dir,
                             std::filesystem::path& written_path,
                             std::string& error);

} // namespace labops::artifacts
