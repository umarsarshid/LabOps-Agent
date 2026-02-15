#pragma once

#include "backends/real_sdk/apply_params.hpp"
#include "core/schema/run_contract.hpp"

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Emits `config_verify.json` for real-backend apply/readback evidence.
//
// This artifact captures per-setting requested vs actual values and support
// status so triage bundles show what really got set.
bool WriteConfigVerifyJson(const core::schema::RunInfo& run_info,
                           const backends::real_sdk::ApplyParamsResult& result,
                           backends::real_sdk::ParamApplyMode mode,
                           const std::filesystem::path& output_dir,
                           std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
