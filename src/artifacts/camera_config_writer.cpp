#include "artifacts/camera_config_writer.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

struct CuratedNodeRow {
  std::string generic_key;
  std::optional<std::string> node_name;
  std::optional<std::string> requested;
  std::optional<std::string> actual;
  bool supported = false;
  bool applied = false;
  bool adjusted = false;
  bool missing = false;
  std::optional<std::string> reason;
};

std::string EscapeJson(std::string_view input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default: {
      const auto as_unsigned = static_cast<unsigned char>(ch);
      if (as_unsigned < 0x20U) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(as_unsigned) << std::dec << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
    }
  }
  return out.str();
}

bool EnsureOutputDir(const fs::path& output_dir, std::string& error) {
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }
  return true;
}

const char* ModeToString(backends::real_sdk::ParamApplyMode mode) {
  switch (mode) {
  case backends::real_sdk::ParamApplyMode::kStrict:
    return "strict";
  case backends::real_sdk::ParamApplyMode::kBestEffort:
    return "best_effort";
  }
  return "strict";
}

std::optional<std::string> ToNonEmptyOptional(const std::string& value) {
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> NormalizeOptionalText(const std::optional<std::string>& value) {
  if (!value.has_value()) {
    return std::nullopt;
  }
  if (value->empty() || *value == "(none)") {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> FindConfigValue(const backends::BackendConfig& backend_dump,
                                           std::string_view key) {
  const auto it = backend_dump.find(std::string(key));
  if (it == backend_dump.end()) {
    return std::nullopt;
  }
  if (it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> ResolveIdentityField(const core::schema::RunInfo& run_info,
                                                const backends::BackendConfig& backend_dump,
                                                std::string_view backend_key,
                                                const std::optional<std::string>& run_info_value) {
  if (const auto normalized = NormalizeOptionalText(run_info_value); normalized.has_value()) {
    return normalized;
  }
  return NormalizeOptionalText(FindConfigValue(backend_dump, backend_key));
}

std::optional<std::string> ResolveIdentityField(const core::schema::RunInfo& run_info,
                                                const backends::BackendConfig& backend_dump,
                                                std::string_view backend_key,
                                                const std::string& run_info_value) {
  return ResolveIdentityField(run_info, backend_dump, backend_key, ToNonEmptyOptional(run_info_value));
}

std::map<std::string, std::string>
BuildRequestedLookup(const std::vector<backends::real_sdk::ApplyParamInput>& requested_params) {
  std::map<std::string, std::string> requested;
  for (const auto& input : requested_params) {
    if (input.generic_key.empty()) {
      continue;
    }
    // Keep the final requested value when duplicate keys appear.
    requested[input.generic_key] = input.requested_value;
  }
  return requested;
}

std::map<std::string, backends::real_sdk::ReadbackRow>
BuildReadbackLookup(const backends::real_sdk::ApplyParamsResult& apply_result) {
  std::map<std::string, backends::real_sdk::ReadbackRow> rows;
  for (const auto& row : apply_result.readback_rows) {
    if (row.generic_key.empty()) {
      continue;
    }
    // Keep the final row to reflect the final observed value/state per key.
    rows[row.generic_key] = row;
  }
  return rows;
}

std::vector<std::string> CuratedGenericKeys() {
  return {
      "frame_rate",
      "pixel_format",
      "exposure",
      "gain",
      "roi",
      "trigger_mode",
      "trigger_source",
  };
}

void SortAndUnique(std::vector<std::string>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::string MissingReason(const std::optional<std::string>& requested_value) {
  if (requested_value.has_value()) {
    return "requested key did not produce a readback row";
  }
  return "key not requested by scenario";
}

std::vector<CuratedNodeRow> BuildCuratedNodeRows(
    const std::vector<std::string>& curated_keys,
    const std::map<std::string, std::string>& requested_by_key,
    const std::map<std::string, backends::real_sdk::ReadbackRow>& readback_by_key,
    std::vector<std::string>& missing_keys, std::vector<std::string>& unsupported_keys) {
  std::vector<CuratedNodeRow> rows;
  rows.reserve(curated_keys.size());

  for (const auto& key : curated_keys) {
    CuratedNodeRow row;
    row.generic_key = key;

    const auto requested_it = requested_by_key.find(key);
    const std::optional<std::string> requested_value =
        requested_it == requested_by_key.end() ? std::nullopt
                                               : ToNonEmptyOptional(requested_it->second);

    const auto readback_it = readback_by_key.find(key);
    if (readback_it == readback_by_key.end()) {
      row.requested = requested_value;
      row.missing = true;
      row.reason = MissingReason(requested_value);
      rows.push_back(std::move(row));
      missing_keys.push_back(key);
      continue;
    }

    const backends::real_sdk::ReadbackRow& readback = readback_it->second;
    row.node_name = ToNonEmptyOptional(readback.node_name);
    row.requested =
        ToNonEmptyOptional(readback.requested_value).has_value()
            ? ToNonEmptyOptional(readback.requested_value)
            : requested_value;
    row.actual = ToNonEmptyOptional(readback.actual_value);
    row.supported = readback.supported;
    row.applied = readback.applied;
    row.adjusted = readback.adjusted;
    row.reason = ToNonEmptyOptional(readback.reason);

    if (!readback.supported || !readback.applied) {
      unsupported_keys.push_back(key);
    }

    rows.push_back(std::move(row));
  }

  return rows;
}

void WriteOptionalString(std::ofstream& out_file, const std::optional<std::string>& value) {
  if (!value.has_value()) {
    out_file << "null";
    return;
  }
  out_file << "\"" << EscapeJson(value.value()) << "\"";
}

void WriteStringArray(std::ofstream& out_file, const std::vector<std::string>& values) {
  out_file << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) {
      out_file << ",";
    }
    out_file << "\"" << EscapeJson(values[i]) << "\"";
  }
  out_file << "]";
}

} // namespace

bool WriteCameraConfigJson(const core::schema::RunInfo& run_info,
                           const backends::BackendConfig& backend_dump,
                           const std::vector<backends::real_sdk::ApplyParamInput>& requested_params,
                           const backends::real_sdk::ApplyParamsResult& apply_result,
                           backends::real_sdk::ParamApplyMode mode,
                           std::string_view collection_error, const fs::path& output_dir,
                           fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  const std::map<std::string, std::string> requested_by_key = BuildRequestedLookup(requested_params);
  const std::map<std::string, backends::real_sdk::ReadbackRow> readback_by_key =
      BuildReadbackLookup(apply_result);

  std::vector<std::string> missing_keys;
  std::vector<std::string> unsupported_keys;
  const std::vector<std::string> curated_keys = CuratedGenericKeys();
  const std::vector<CuratedNodeRow> curated_rows = BuildCuratedNodeRows(
      curated_keys, requested_by_key, readback_by_key, missing_keys, unsupported_keys);

  for (const auto& [key, row] : readback_by_key) {
    if (!row.supported || !row.applied) {
      unsupported_keys.push_back(key);
    }
  }
  SortAndUnique(missing_keys);
  SortAndUnique(unsupported_keys);

  std::vector<std::string> missing_requested_keys;
  for (const auto& [key, value] : requested_by_key) {
    (void)value;
    if (readback_by_key.find(key) == readback_by_key.end()) {
      missing_requested_keys.push_back(key);
    }
  }
  SortAndUnique(missing_requested_keys);

  std::optional<std::string> model;
  std::optional<std::string> serial;
  std::optional<std::string> transport;
  std::optional<std::string> user_id;
  std::optional<std::string> firmware_version;
  std::optional<std::string> sdk_version;
  if (run_info.real_device.has_value()) {
    model = ResolveIdentityField(run_info, backend_dump, "device.model",
                                 run_info.real_device->model);
    serial = ResolveIdentityField(run_info, backend_dump, "device.serial",
                                  run_info.real_device->serial);
    transport = ResolveIdentityField(run_info, backend_dump, "device.transport",
                                     run_info.real_device->transport);
    user_id =
        ResolveIdentityField(run_info, backend_dump, "device.user_id", run_info.real_device->user_id);
    firmware_version = ResolveIdentityField(run_info, backend_dump, "device.firmware_version",
                                            run_info.real_device->firmware_version);
    sdk_version = ResolveIdentityField(run_info, backend_dump, "device.sdk_version",
                                       run_info.real_device->sdk_version);
  } else {
    model = NormalizeOptionalText(FindConfigValue(backend_dump, "device.model"));
    serial = NormalizeOptionalText(FindConfigValue(backend_dump, "device.serial"));
    transport = NormalizeOptionalText(FindConfigValue(backend_dump, "device.transport"));
    user_id = NormalizeOptionalText(FindConfigValue(backend_dump, "device.user_id"));
    firmware_version = NormalizeOptionalText(FindConfigValue(backend_dump, "device.firmware_version"));
    sdk_version = NormalizeOptionalText(FindConfigValue(backend_dump, "device.sdk_version"));
  }
  const std::optional<std::string> selector =
      NormalizeOptionalText(FindConfigValue(backend_dump, "device.selector"));
  const std::optional<std::string> index =
      NormalizeOptionalText(FindConfigValue(backend_dump, "device.index"));
  const std::optional<std::string> ip_address =
      NormalizeOptionalText(FindConfigValue(backend_dump, "device.ip"));
  const std::optional<std::string> mac_address =
      NormalizeOptionalText(FindConfigValue(backend_dump, "device.mac"));

  written_path = output_dir / "camera_config.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out_file << "{\n"
           << "  \"schema_version\":\"1.0\",\n"
           << "  \"run_id\":\"" << EscapeJson(run_info.run_id) << "\",\n"
           << "  \"scenario_id\":\"" << EscapeJson(run_info.config.scenario_id) << "\",\n"
           << "  \"backend\":\"" << EscapeJson(run_info.config.backend) << "\",\n"
           << "  \"apply_mode\":\"" << ModeToString(mode) << "\",\n"
           << "  \"collection_error\":";
  if (collection_error.empty()) {
    out_file << "null,\n";
  } else {
    out_file << "\"" << EscapeJson(collection_error) << "\",\n";
  }

  out_file << "  \"identity\":{\n"
           << "    \"model\":";
  WriteOptionalString(out_file, model);
  out_file << ",\n"
           << "    \"serial\":";
  WriteOptionalString(out_file, serial);
  out_file << ",\n"
           << "    \"transport\":";
  WriteOptionalString(out_file, transport);
  out_file << ",\n"
           << "    \"user_id\":";
  WriteOptionalString(out_file, user_id);
  out_file << ",\n"
           << "    \"firmware_version\":";
  WriteOptionalString(out_file, firmware_version);
  out_file << ",\n"
           << "    \"sdk_version\":";
  WriteOptionalString(out_file, sdk_version);
  out_file << ",\n"
           << "    \"selector\":";
  WriteOptionalString(out_file, selector);
  out_file << ",\n"
           << "    \"index\":";
  WriteOptionalString(out_file, index);
  out_file << ",\n"
           << "    \"ip\":";
  WriteOptionalString(out_file, ip_address);
  out_file << ",\n"
           << "    \"mac\":";
  WriteOptionalString(out_file, mac_address);
  out_file << "\n"
           << "  },\n";

  out_file << "  \"curated_nodes\":[";
  for (std::size_t i = 0; i < curated_rows.size(); ++i) {
    const CuratedNodeRow& row = curated_rows[i];
    if (i != 0U) {
      out_file << ",";
    }
    out_file << "\n    {"
             << "\"generic_key\":\"" << EscapeJson(row.generic_key) << "\","
             << "\"node_name\":";
    WriteOptionalString(out_file, row.node_name);
    out_file << ",\"requested\":";
    WriteOptionalString(out_file, row.requested);
    out_file << ",\"actual\":";
    WriteOptionalString(out_file, row.actual);
    out_file << ",\"supported\":" << (row.supported ? "true" : "false")
             << ",\"applied\":" << (row.applied ? "true" : "false")
             << ",\"adjusted\":" << (row.adjusted ? "true" : "false")
             << ",\"missing\":" << (row.missing ? "true" : "false")
             << ",\"reason\":";
    WriteOptionalString(out_file, row.reason);
    out_file << "}";
  }
  out_file << "\n  ],\n"
           << "  \"missing_keys\":";
  WriteStringArray(out_file, missing_keys);
  out_file << ",\n"
           << "  \"missing_requested_keys\":";
  WriteStringArray(out_file, missing_requested_keys);
  out_file << ",\n"
           << "  \"unsupported_keys\":";
  WriteStringArray(out_file, unsupported_keys);
  out_file << ",\n";

  out_file << "  \"backend_dump\":{";
  std::size_t emitted_count = 0;
  for (const auto& [key, value] : backend_dump) {
    if (emitted_count != 0U) {
      out_file << ",";
    }
    out_file << "\n    \"" << EscapeJson(key) << "\":\"" << EscapeJson(value) << "\"";
    ++emitted_count;
  }
  if (emitted_count != 0U) {
    out_file << "\n  ";
  }
  out_file << "}\n"
           << "}\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
