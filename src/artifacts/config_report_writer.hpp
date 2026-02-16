#pragma once

#include "backends/real_sdk/apply_params.hpp"
#include "core/schema/run_contract.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace labops::artifacts {

// Emits `config_report.md` for real-backend runs.
//
// This report is intentionally human-first and summarizes per-setting apply
// outcomes in one table:
// - ✅ applied
// - ⚠ adjusted (constraints)
// - ❌ unsupported
//
// `collection_error` captures upstream failures (for example key-map loading)
// so engineers can quickly see why row-level evidence might be incomplete.
bool WriteConfigReportMarkdown(
    const core::schema::RunInfo& run_info,
    const std::vector<backends::real_sdk::ApplyParamInput>& requested_params,
    const backends::real_sdk::ApplyParamsResult& apply_result,
    backends::real_sdk::ParamApplyMode mode, std::string_view collection_error,
    const std::filesystem::path& output_dir, std::filesystem::path& written_path,
    std::string& error);

} // namespace labops::artifacts
