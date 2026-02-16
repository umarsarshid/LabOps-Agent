#include "artifacts/config_report_writer.hpp"

#include <algorithm>
#include <ctime>
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

struct ReportRow {
  std::string generic_key;
  std::string node_name;
  std::string requested;
  std::string actual;
  std::string status_icon;
  std::string status_text;
  std::string notes;
  bool supported = false;
  bool applied = false;
  bool adjusted = false;
};

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

std::string FormatUtcTimestamp(std::chrono::system_clock::time_point timestamp) {
  const auto millis_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
  const auto millis_component = static_cast<int>((millis_since_epoch % 1000 + 1000) % 1000);

  const std::time_t epoch_seconds = std::chrono::system_clock::to_time_t(timestamp);
  std::tm utc_time{};
#if defined(_WIN32)
  const errno_t result = gmtime_s(&utc_time, &epoch_seconds);
  if (result != 0) {
    return "";
  }
#else
  const std::tm* result = gmtime_r(&epoch_seconds, &utc_time);
  if (result == nullptr) {
    return "";
  }
#endif

  std::ostringstream out;
  out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis_component << 'Z';
  return out.str();
}

std::string EscapeMarkdownCell(std::string value) {
  // Keep table columns stable even when values contain markdown separators or
  // multiline messages from backend/node validation.
  for (char& ch : value) {
    if (ch == '\n' || ch == '\r') {
      ch = ' ';
    }
  }

  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char ch : value) {
    if (ch == '|') {
      escaped += "\\|";
      continue;
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string NormalizeCellValue(const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty()) {
    return "-";
  }
  return value.value();
}

std::string NormalizeCellValue(const std::string& value) {
  if (value.empty()) {
    return "-";
  }
  return value;
}

std::map<std::string, std::string>
BuildRequestedLookup(const std::vector<backends::real_sdk::ApplyParamInput>& requested_params) {
  std::map<std::string, std::string> requested_by_key;
  for (const auto& input : requested_params) {
    if (input.generic_key.empty()) {
      continue;
    }
    requested_by_key[input.generic_key] = input.requested_value;
  }
  return requested_by_key;
}

std::vector<ReportRow>
BuildReportRows(const std::vector<backends::real_sdk::ApplyParamInput>& requested_params,
                const backends::real_sdk::ApplyParamsResult& apply_result) {
  const std::map<std::string, std::string> requested_by_key =
      BuildRequestedLookup(requested_params);
  std::vector<ReportRow> rows;
  rows.reserve(apply_result.readback_rows.size());

  for (const auto& readback : apply_result.readback_rows) {
    ReportRow row;
    row.generic_key = readback.generic_key.empty() ? "-" : readback.generic_key;
    row.node_name = NormalizeCellValue(readback.node_name);
    row.requested = NormalizeCellValue(readback.requested_value);
    row.actual = NormalizeCellValue(readback.actual_value);
    row.supported = readback.supported;
    row.applied = readback.applied;
    row.adjusted = readback.adjusted;

    if (!readback.supported || !readback.applied) {
      row.status_icon = "❌";
      row.status_text = "unsupported";
      row.notes = NormalizeCellValue(readback.reason);
    } else if (readback.adjusted) {
      row.status_icon = "⚠";
      row.status_text = "adjusted";
      if (readback.reason.empty()) {
        row.notes = "adjusted due to backend constraints";
      } else {
        row.notes = readback.reason;
      }
    } else {
      row.status_icon = "✅";
      row.status_text = "applied";
      row.notes = NormalizeCellValue(readback.reason);
    }

    const auto requested_it = requested_by_key.find(readback.generic_key);
    if (requested_it != requested_by_key.end() && !requested_it->second.empty()) {
      row.requested = requested_it->second;
    }

    rows.push_back(std::move(row));
  }

  std::sort(rows.begin(), rows.end(), [](const ReportRow& lhs, const ReportRow& rhs) {
    if (lhs.generic_key == rhs.generic_key) {
      return lhs.node_name < rhs.node_name;
    }
    return lhs.generic_key < rhs.generic_key;
  });
  return rows;
}

void WriteSummarySection(std::ofstream& out_file, const std::vector<ReportRow>& rows) {
  std::size_t applied_count = 0;
  std::size_t adjusted_count = 0;
  std::size_t unsupported_count = 0;
  for (const auto& row : rows) {
    if (row.status_text == "applied") {
      ++applied_count;
      continue;
    }
    if (row.status_text == "adjusted") {
      ++adjusted_count;
      continue;
    }
    ++unsupported_count;
  }

  out_file << "## Summary\n\n";
  out_file << "- ✅ applied: " << applied_count << '\n';
  out_file << "- ⚠ adjusted: " << adjusted_count << '\n';
  out_file << "- ❌ unsupported: " << unsupported_count << "\n\n";
}

void WriteConfigTable(std::ofstream& out_file, const std::vector<ReportRow>& rows) {
  out_file << "## Config Table\n\n";
  out_file << "| Status | Key | Node | Requested | Actual | Notes |\n";
  out_file << "| --- | --- | --- | --- | --- | --- |\n";

  if (rows.empty()) {
    out_file << "| ❌ unsupported | - | - | - | - | no config rows were captured |\n\n";
    return;
  }

  for (const auto& row : rows) {
    out_file << "| " << row.status_icon << " " << row.status_text << " | "
             << EscapeMarkdownCell(row.generic_key) << " | " << EscapeMarkdownCell(row.node_name)
             << " | " << EscapeMarkdownCell(row.requested) << " | "
             << EscapeMarkdownCell(row.actual) << " | "
             << EscapeMarkdownCell(NormalizeCellValue(row.notes)) << " |\n";
  }
  out_file << '\n';
}

} // namespace

bool WriteConfigReportMarkdown(
    const core::schema::RunInfo& run_info,
    const std::vector<backends::real_sdk::ApplyParamInput>& requested_params,
    const backends::real_sdk::ApplyParamsResult& apply_result,
    backends::real_sdk::ParamApplyMode mode, std::string_view collection_error,
    const fs::path& output_dir, fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  const std::vector<ReportRow> rows = BuildReportRows(requested_params, apply_result);

  written_path = output_dir / "config_report.md";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out_file << "# Config Report\n\n";
  out_file << "## Run\n\n";
  out_file << "- run_id: `" << run_info.run_id << "`\n";
  out_file << "- scenario_id: `" << run_info.config.scenario_id << "`\n";
  out_file << "- backend: `" << run_info.config.backend << "`\n";
  out_file << "- apply_mode: `" << ModeToString(mode) << "`\n";
  out_file << "- started_at_utc: `" << FormatUtcTimestamp(run_info.timestamps.started_at) << "`\n";
  out_file << "- finished_at_utc: `" << FormatUtcTimestamp(run_info.timestamps.finished_at)
           << "`\n";
  out_file << '\n';

  if (!collection_error.empty()) {
    out_file << "## Collection Notes\n\n";
    out_file << "- config collection error: " << EscapeMarkdownCell(std::string(collection_error))
             << "\n\n";
  }

  WriteSummarySection(out_file, rows);
  WriteConfigTable(out_file, rows);

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
