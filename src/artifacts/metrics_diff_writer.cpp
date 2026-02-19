#include "artifacts/metrics_diff_writer.hpp"

#include "core/json_utils.hpp"
#include "core/time_utils.hpp"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

constexpr std::string_view kMetricsCsvHeader = "metric,window_end_ms,window_ms,frames,fps";

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

void TrimTrailingCarriageReturn(std::string& line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
}

bool ParseDouble(std::string_view text, double& value) {
  std::string owned(text);
  char* parse_end = nullptr;
  const double parsed = std::strtod(owned.c_str(), &parse_end);
  if (parse_end == nullptr || *parse_end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
  std::vector<std::string> columns;
  std::string current;

  // Current metrics CSV does not use quoted commas; keep parser simple and
  // strict so contract drift is caught early.
  for (const char ch : line) {
    if (ch == ',') {
      columns.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }

  columns.push_back(current);
  return columns;
}

bool LoadSummaryMetricsFromCsv(const fs::path& metrics_csv_path,
                               std::map<std::string, double>& metric_values, std::string& error) {
  std::ifstream input(metrics_csv_path, std::ios::binary);
  if (!input) {
    error = "failed to open metrics csv: " + metrics_csv_path.string();
    return false;
  }

  std::string header;
  if (!std::getline(input, header)) {
    error = "metrics csv is empty: " + metrics_csv_path.string();
    return false;
  }
  TrimTrailingCarriageReturn(header);
  if (header != kMetricsCsvHeader) {
    error = "metrics csv header mismatch for file: " + metrics_csv_path.string();
    return false;
  }

  metric_values.clear();
  std::string line;
  std::size_t line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    TrimTrailingCarriageReturn(line);
    if (line.empty()) {
      continue;
    }

    const std::vector<std::string> columns = SplitCsvLine(line);
    if (columns.size() != 5U) {
      error = "invalid metrics csv row at line " + std::to_string(line_number) +
              " in file: " + metrics_csv_path.string();
      return false;
    }

    const std::string& metric_name = columns[0];
    if (metric_name.empty()) {
      error = "empty metric name at line " + std::to_string(line_number) +
              " in file: " + metrics_csv_path.string();
      return false;
    }

    if (metric_name == "rolling_fps") {
      continue;
    }

    double metric_value = 0.0;
    if (!ParseDouble(columns[4], metric_value)) {
      error = "invalid metric value at line " + std::to_string(line_number) +
              " in file: " + metrics_csv_path.string();
      return false;
    }

    const auto insert_result = metric_values.insert({metric_name, metric_value});
    if (!insert_result.second) {
      error = "duplicate summary metric row for '" + metric_name +
              "' in file: " + metrics_csv_path.string();
      return false;
    }
  }

  if (metric_values.empty()) {
    error = "no summary metrics found in file: " + metrics_csv_path.string();
    return false;
  }

  return true;
}

std::vector<std::string> BuildPreferredMetricOrder() {
  return {
      "avg_fps",
      "drops_total",
      "drops_generic_total",
      "timeouts_total",
      "incomplete_total",
      "drop_rate_percent",
      "generic_drop_rate_percent",
      "timeout_rate_percent",
      "incomplete_rate_percent",
      "inter_frame_interval_min_us",
      "inter_frame_interval_avg_us",
      "inter_frame_interval_p95_us",
      "inter_frame_jitter_min_us",
      "inter_frame_jitter_avg_us",
      "inter_frame_jitter_p95_us",
  };
}

bool ShouldTreatAsZero(double value) {
  return std::fabs(value) <= 1e-12;
}

void CountDeltaSummary(const std::vector<MetricDelta>& deltas, std::size_t& increased,
                       std::size_t& decreased, std::size_t& unchanged) {
  increased = 0;
  decreased = 0;
  unchanged = 0;

  for (const auto& delta : deltas) {
    if (delta.delta > 1e-12) {
      ++increased;
      continue;
    }
    if (delta.delta < -1e-12) {
      ++decreased;
      continue;
    }
    ++unchanged;
  }
}

} // namespace

bool ComputeMetricsDiffFromCsv(const fs::path& baseline_metrics_csv_path,
                               const fs::path& run_metrics_csv_path, MetricsDiffReport& report,
                               std::string& error) {
  std::map<std::string, double> baseline_values;
  if (!LoadSummaryMetricsFromCsv(baseline_metrics_csv_path, baseline_values, error)) {
    return false;
  }

  std::map<std::string, double> run_values;
  if (!LoadSummaryMetricsFromCsv(run_metrics_csv_path, run_values, error)) {
    return false;
  }

  const std::vector<std::string> preferred_order = BuildPreferredMetricOrder();
  std::set<std::string> remaining_intersection;
  for (const auto& [metric_name, baseline_value] : baseline_values) {
    (void)baseline_value;
    if (run_values.find(metric_name) != run_values.end()) {
      remaining_intersection.insert(metric_name);
    }
  }

  report.baseline_metrics_csv_path = baseline_metrics_csv_path;
  report.run_metrics_csv_path = run_metrics_csv_path;
  report.deltas.clear();

  auto append_metric_delta = [&](const std::string& metric_name) {
    const auto baseline_it = baseline_values.find(metric_name);
    const auto run_it = run_values.find(metric_name);
    if (baseline_it == baseline_values.end() || run_it == run_values.end()) {
      return;
    }

    MetricDelta delta;
    delta.metric = metric_name;
    delta.baseline = baseline_it->second;
    delta.run = run_it->second;
    delta.delta = delta.run - delta.baseline;

    if (ShouldTreatAsZero(delta.baseline)) {
      if (ShouldTreatAsZero(delta.run)) {
        delta.delta_percent = 0.0;
      }
    } else {
      delta.delta_percent = (delta.delta / delta.baseline) * 100.0;
    }

    report.deltas.push_back(delta);
    remaining_intersection.erase(metric_name);
  };

  for (const auto& metric_name : preferred_order) {
    append_metric_delta(metric_name);
  }

  for (const auto& metric_name : remaining_intersection) {
    append_metric_delta(metric_name);
  }

  if (report.deltas.empty()) {
    error = "no overlapping summary metrics to compare";
    return false;
  }

  return true;
}

bool WriteMetricsDiffJson(const MetricsDiffReport& report, const fs::path& output_dir,
                          fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "diff.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  std::size_t increased = 0;
  std::size_t decreased = 0;
  std::size_t unchanged = 0;
  CountDeltaSummary(report.deltas, increased, decreased, unchanged);

  out_file << "{\n"
           << "  \"schema_version\":\"1.0\",\n"
           << "  \"baseline_metrics_csv\":\""
           << core::EscapeJson(report.baseline_metrics_csv_path.generic_string()) << "\",\n"
           << "  \"run_metrics_csv\":\""
           << core::EscapeJson(report.run_metrics_csv_path.generic_string()) << "\",\n"
           << "  \"compared_metrics\":[";

  for (std::size_t i = 0; i < report.deltas.size(); ++i) {
    const auto& delta = report.deltas[i];
    if (i != 0U) {
      out_file << ",";
    }
    out_file << "\n    {"
             << "\"metric\":\"" << core::EscapeJson(delta.metric) << "\","
             << "\"baseline\":" << core::FormatFixedDouble(delta.baseline, 6) << ","
             << "\"run\":" << core::FormatFixedDouble(delta.run, 6) << ","
             << "\"delta\":" << core::FormatFixedDouble(delta.delta, 6) << ","
             << "\"delta_percent\":";

    if (delta.delta_percent.has_value()) {
      out_file << core::FormatFixedDouble(delta.delta_percent.value(), 6);
    } else {
      out_file << "null";
    }

    out_file << "}";
  }

  out_file << "\n  ],\n"
           << "  \"summary\":{"
           << "\"increased\":" << increased << ","
           << "\"decreased\":" << decreased << ","
           << "\"unchanged\":" << unchanged << "}\n"
           << "}\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

bool WriteMetricsDiffMarkdown(const MetricsDiffReport& report, const fs::path& output_dir,
                              fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "diff.md";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  std::size_t increased = 0;
  std::size_t decreased = 0;
  std::size_t unchanged = 0;
  CountDeltaSummary(report.deltas, increased, decreased, unchanged);

  out_file << "# Metrics Diff\n\n"
           << "Baseline metrics: `" << report.baseline_metrics_csv_path.generic_string() << "`\n\n"
           << "Run metrics: `" << report.run_metrics_csv_path.generic_string() << "`\n\n"
           << "| Metric | Baseline | Run | Delta | Delta % |\n"
           << "| --- | ---: | ---: | ---: | ---: |\n";

  for (const auto& delta : report.deltas) {
    out_file << "| " << delta.metric << " | " << core::FormatFixedDouble(delta.baseline, 6) << " | "
             << core::FormatFixedDouble(delta.run, 6) << " | " << (delta.delta >= 0.0 ? "+" : "")
             << core::FormatFixedDouble(delta.delta, 6) << " | ";

    if (delta.delta_percent.has_value()) {
      const double delta_percent = delta.delta_percent.value();
      out_file << (delta_percent >= 0.0 ? "+" : "") << core::FormatFixedDouble(delta_percent, 6)
               << "%";
    } else {
      out_file << "n/a";
    }

    out_file << " |\n";
  }

  out_file << "\n"
           << "Summary: increased=" << increased << ", decreased=" << decreased
           << ", unchanged=" << unchanged << "\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
