#pragma once

#include "backends/camera_backend.hpp"
#include "backends/real_sdk/apply_params.hpp"
#include "core/schema/run_contract.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace labops::artifacts {

// Emits `camera_config.json` for real-backend runs.
//
// This artifact is intended to be the engineer-readable config report:
// - resolved camera identity fields
// - curated camera setting rows (requested/actual/support/apply state)
// - missing and unsupported key lists
// - raw backend config snapshot for low-level debugging
//
// `collection_error` should describe upstream apply/collection failures when
// available. Pass an empty string when collection succeeded.
bool WriteCameraConfigJson(const core::schema::RunInfo& run_info,
                           const backends::BackendConfig& backend_dump,
                           const std::vector<backends::real_sdk::ApplyParamInput>& requested_params,
                           const backends::real_sdk::ApplyParamsResult& apply_result,
                           backends::real_sdk::ParamApplyMode mode,
                           std::string_view collection_error,
                           const std::filesystem::path& output_dir,
                           std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
