#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace labops::artifacts {

// One metric comparison result between a baseline and a run.
struct MetricDelta {
  std::string metric;
  double baseline = 0.0;
  double run = 0.0;
  double delta = 0.0;
  std::optional<double> delta_percent;
};

// Comparison payload used by JSON/Markdown diff writers.
struct MetricsDiffReport {
  std::filesystem::path baseline_metrics_csv_path;
  std::filesystem::path run_metrics_csv_path;
  std::vector<MetricDelta> deltas;
};

// Loads baseline/run metrics CSV files and computes per-metric deltas.
//
// Contract:
// - both paths must point to readable metrics.csv files.
// - compares summary metrics (non-rolling rows) by metric name.
// - returns false and sets `error` on parse/contract failures.
bool ComputeMetricsDiffFromCsv(const std::filesystem::path& baseline_metrics_csv_path,
                               const std::filesystem::path& run_metrics_csv_path,
                               MetricsDiffReport& report, std::string& error);

// Emits `diff.json` for machine parsing.
//
// Contract:
// - creates `output_dir` when missing.
// - writes `<output_dir>/diff.json`.
// - returns false and sets `error` on failure.
bool WriteMetricsDiffJson(const MetricsDiffReport& report, const std::filesystem::path& output_dir,
                          std::filesystem::path& written_path, std::string& error);

// Emits `diff.md` for human triage handoff.
//
// Contract:
// - creates `output_dir` when missing.
// - writes `<output_dir>/diff.md`.
// - returns false and sets `error` on failure.
bool WriteMetricsDiffMarkdown(const MetricsDiffReport& report,
                              const std::filesystem::path& output_dir,
                              std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
