#include "artifacts/config_report_writer.hpp"
#include "artifacts/output_dir_utils.hpp"
#include "core/time_utils.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

// Keep status typed end-to-end so summary counts and table labels cannot drift
// due to string typos.
enum class ReportStatus {
  kApplied = 0,
  kAdjusted,
  kUnsupported,
};

struct ReportRow {
  std::string generic_key;
  std::string node_name;
  std::string requested;
  std::string actual;
  std::string notes;
  ReportStatus status = ReportStatus::kUnsupported;
};

const char* ModeToString(backends::real_sdk::ParamApplyMode mode) {
  switch (mode) {
  case backends::real_sdk::ParamApplyMode::kStrict:
    return "strict";
  case backends::real_sdk::ParamApplyMode::kBestEffort:
    return "best_effort";
  }
  return "strict";
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

std::optional<std::string> BuildKeyUnitAndRangeNote(std::string_view generic_key) {
  // Exposure and gain are high-touch knobs in camera triage tickets.
  // Keep unit/range hints inline so engineers can sanity-check values without
  // cross-referencing schema docs during first-pass review.
  if (generic_key == "exposure") {
    return "units: us; validated range: [5, 10000000]";
  }
  if (generic_key == "width") {
    return "units: px; negotiated via VIDIOC_S_FMT";
  }
  if (generic_key == "height") {
    return "units: px; negotiated via VIDIOC_S_FMT";
  }
  if (generic_key == "fps") {
    return "units: Hz; negotiated via VIDIOC_S_PARM when supported";
  }
  if (generic_key == "gain") {
    return "units: dB; validated range: [0, 48]";
  }
  if (generic_key == "packet_size_bytes") {
    return "units: bytes; GigE-only; validated range: [576, 9000]";
  }
  if (generic_key == "inter_packet_delay_us") {
    return "units: us; GigE-only; validated range: [0, 100000]";
  }
  if (generic_key == "roi_width") {
    return "units: px; validated range: [64, 4096]; applied before offsets";
  }
  if (generic_key == "roi_height") {
    return "units: px; validated range: [64, 2160]; applied before offsets";
  }
  if (generic_key == "roi_offset_x") {
    return "units: px; validated range: [0, 4095]; applied after width/height";
  }
  if (generic_key == "roi_offset_y") {
    return "units: px; validated range: [0, 2159]; applied after width/height";
  }
  return std::nullopt;
}

void AppendKeyUnitAndRangeNote(std::string_view generic_key, std::string& notes) {
  const std::optional<std::string> key_note = BuildKeyUnitAndRangeNote(generic_key);
  if (!key_note.has_value()) {
    return;
  }

  if (notes.empty() || notes == "-") {
    notes = key_note.value();
    return;
  }
  notes += "; " + key_note.value();
}

ReportStatus ClassifyReportStatus(const backends::real_sdk::ReadbackRow& row) {
  if (!row.supported || !row.applied) {
    return ReportStatus::kUnsupported;
  }
  if (row.adjusted) {
    return ReportStatus::kAdjusted;
  }
  return ReportStatus::kApplied;
}

const char* ReportStatusIcon(ReportStatus status) {
  switch (status) {
  case ReportStatus::kApplied:
    return "✅";
  case ReportStatus::kAdjusted:
    return "⚠";
  case ReportStatus::kUnsupported:
    return "❌";
  }
  return "❌";
}

const char* ReportStatusText(ReportStatus status) {
  switch (status) {
  case ReportStatus::kApplied:
    return "applied";
  case ReportStatus::kAdjusted:
    return "adjusted";
  case ReportStatus::kUnsupported:
    return "unsupported";
  }
  return "unsupported";
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
    row.status = ClassifyReportStatus(readback);

    if (row.status == ReportStatus::kUnsupported) {
      row.notes = NormalizeCellValue(readback.reason);
    } else if (row.status == ReportStatus::kAdjusted) {
      if (readback.reason.empty()) {
        row.notes = "adjusted due to backend constraints";
      } else {
        row.notes = readback.reason;
      }
    } else {
      row.notes = NormalizeCellValue(readback.reason);
    }

    const auto requested_it = requested_by_key.find(readback.generic_key);
    if (requested_it != requested_by_key.end() && !requested_it->second.empty()) {
      row.requested = requested_it->second;
    }

    AppendKeyUnitAndRangeNote(row.generic_key, row.notes);

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
    if (row.status == ReportStatus::kApplied) {
      ++applied_count;
      continue;
    }
    if (row.status == ReportStatus::kAdjusted) {
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
    out_file << "| " << ReportStatusIcon(row.status) << " " << ReportStatusText(row.status) << " | "
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
  out_file << "- started_at_utc: `" << core::FormatUtcTimestamp(run_info.timestamps.started_at)
           << "`\n";
  out_file << "- finished_at_utc: `" << core::FormatUtcTimestamp(run_info.timestamps.finished_at)
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
