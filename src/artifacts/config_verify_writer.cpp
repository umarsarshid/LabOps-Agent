#include "artifacts/config_verify_writer.hpp"
#include "artifacts/output_dir_utils.hpp"
#include "core/json_utils.hpp"

#include <fstream>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

const char* ModeToString(backends::real_sdk::ParamApplyMode mode) {
  switch (mode) {
  case backends::real_sdk::ParamApplyMode::kStrict:
    return "strict";
  case backends::real_sdk::ParamApplyMode::kBestEffort:
    return "best_effort";
  }
  return "strict";
}

} // namespace

bool WriteConfigVerifyJson(const core::schema::RunInfo& run_info,
                           const backends::real_sdk::ApplyParamsResult& result,
                           backends::real_sdk::ParamApplyMode mode, const fs::path& output_dir,
                           fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  std::size_t supported_count = 0;
  std::size_t applied_count = 0;
  std::size_t adjusted_count = 0;
  for (const auto& row : result.readback_rows) {
    if (row.supported) {
      ++supported_count;
    }
    if (row.applied) {
      ++applied_count;
    }
    if (row.adjusted) {
      ++adjusted_count;
    }
  }

  written_path = output_dir / "config_verify.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out_file << "{\n"
           << "  \"schema_version\":\"1.0\",\n"
           << "  \"run_id\":\"" << core::EscapeJson(run_info.run_id) << "\",\n"
           << "  \"scenario_id\":\"" << core::EscapeJson(run_info.config.scenario_id) << "\",\n"
           << "  \"backend\":\"" << core::EscapeJson(run_info.config.backend) << "\",\n"
           << "  \"apply_mode\":\"" << ModeToString(mode) << "\",\n"
           << "  \"summary\":{\n"
           << "    \"requested_count\":" << result.readback_rows.size() << ",\n"
           << "    \"supported_count\":" << supported_count << ",\n"
           << "    \"unsupported_count\":" << (result.readback_rows.size() - supported_count)
           << ",\n"
           << "    \"applied_count\":" << applied_count << ",\n"
           << "    \"unapplied_count\":" << (result.readback_rows.size() - applied_count) << ",\n"
           << "    \"adjusted_count\":" << adjusted_count << "\n"
           << "  },\n"
           << "  \"rows\":[";

  for (std::size_t i = 0; i < result.readback_rows.size(); ++i) {
    const auto& row = result.readback_rows[i];
    if (i != 0U) {
      out_file << ",";
    }
    out_file << "\n    {"
             << "\"generic_key\":\"" << core::EscapeJson(row.generic_key) << "\","
             << "\"node_name\":";
    if (row.node_name.empty()) {
      out_file << "null,";
    } else {
      out_file << "\"" << core::EscapeJson(row.node_name) << "\",";
    }
    out_file << "\"requested\":\"" << core::EscapeJson(row.requested_value) << "\","
             << "\"actual\":";
    if (row.actual_value.empty()) {
      out_file << "null,";
    } else {
      out_file << "\"" << core::EscapeJson(row.actual_value) << "\",";
    }
    out_file << "\"supported\":" << (row.supported ? "true" : "false") << ","
             << "\"applied\":" << (row.applied ? "true" : "false") << ","
             << "\"adjusted\":" << (row.adjusted ? "true" : "false") << ","
             << "\"reason\":";
    if (row.reason.empty()) {
      out_file << "null";
    } else {
      out_file << "\"" << core::EscapeJson(row.reason) << "\"";
    }
    out_file << "}";
  }
  out_file << "\n  ]\n"
           << "}\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }
  return true;
}

} // namespace labops::artifacts
