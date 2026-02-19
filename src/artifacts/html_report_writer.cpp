#include "artifacts/html_report_writer.hpp"

#include "core/time_utils.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

struct DeltaRow {
  std::string metric;
  std::string unit;
  double actual = 0.0;
  double expected = 0.0;
  double delta = 0.0;
};

std::string EscapeHtml(std::string_view input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
    case '&':
      out << "&amp;";
      break;
    case '<':
      out << "&lt;";
      break;
    case '>':
      out << "&gt;";
      break;
    case '"':
      out << "&quot;";
      break;
    case '\'':
      out << "&#39;";
      break;
    default:
      out << ch;
      break;
    }
  }
  return out.str();
}

std::string FormatSignedDouble(const double value, const int precision) {
  if (value >= 0.0) {
    return "+" + core::FormatFixedDouble(value, precision);
  }
  return core::FormatFixedDouble(value, precision);
}

std::string StatusCssClass(const bool thresholds_passed) {
  return thresholds_passed ? "pass" : "fail";
}

std::string StatusLabel(const bool thresholds_passed) {
  return thresholds_passed ? "PASS" : "FAIL";
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

std::vector<DeltaRow> BuildDeltaRows(const metrics::FpsReport& report,
                                     const std::uint32_t configured_fps) {
  const double expected_interval_us =
      configured_fps > 0U ? (1'000'000.0 / static_cast<double>(configured_fps)) : 0.0;

  std::vector<DeltaRow> rows;
  rows.push_back({
      .metric = "avg_fps",
      .unit = "fps",
      .actual = report.avg_fps,
      .expected = static_cast<double>(configured_fps),
      .delta = report.avg_fps - static_cast<double>(configured_fps),
  });
  rows.push_back({
      .metric = "drop_rate_percent",
      .unit = "%",
      .actual = report.drop_rate_percent,
      .expected = 0.0,
      .delta = report.drop_rate_percent,
  });
  rows.push_back({
      .metric = "generic_drop_rate_percent",
      .unit = "%",
      .actual = report.generic_drop_rate_percent,
      .expected = 0.0,
      .delta = report.generic_drop_rate_percent,
  });
  rows.push_back({
      .metric = "timeout_rate_percent",
      .unit = "%",
      .actual = report.timeout_rate_percent,
      .expected = 0.0,
      .delta = report.timeout_rate_percent,
  });
  rows.push_back({
      .metric = "incomplete_rate_percent",
      .unit = "%",
      .actual = report.incomplete_rate_percent,
      .expected = 0.0,
      .delta = report.incomplete_rate_percent,
  });
  rows.push_back({
      .metric = "inter_frame_interval_p95_us",
      .unit = "us",
      .actual = report.inter_frame_interval_us.p95_us,
      .expected = expected_interval_us,
      .delta = report.inter_frame_interval_us.p95_us - expected_interval_us,
  });
  rows.push_back({
      .metric = "inter_frame_jitter_p95_us",
      .unit = "us",
      .actual = report.inter_frame_jitter_us.p95_us,
      .expected = 0.0,
      .delta = report.inter_frame_jitter_us.p95_us,
  });

  return rows;
}

} // namespace

bool WriteRunSummaryHtml(const core::schema::RunInfo& run_info, const metrics::FpsReport& report,
                         const std::uint32_t configured_fps, const bool thresholds_passed,
                         const std::vector<std::string>& threshold_failures,
                         const std::vector<std::string>& top_anomalies, const fs::path& output_dir,
                         fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "report.html";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  const std::vector<DeltaRow> delta_rows = BuildDeltaRows(report, configured_fps);

  out_file << "<!doctype html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "  <meta charset=\"utf-8\" />\n"
           << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
           << "  <title>LabOps Run Report</title>\n"
           << "  <style>\n"
           << "    :root { color-scheme: light; }\n"
           << "    body { font-family: \"Segoe UI\", \"Helvetica Neue\", Arial, sans-serif; "
              "margin: 24px; color: "
              "#1f2933; }\n"
           << "    h1, h2 { margin-bottom: 8px; }\n"
           << "    .meta { color: #52606d; margin-top: 0; }\n"
           << "    .status { display: inline-block; padding: 4px 10px; border-radius: 12px; "
              "font-weight: 700; }\n"
           << "    .status.pass { background: #e8f5e9; color: #1b5e20; }\n"
           << "    .status.fail { background: #ffebee; color: #b71c1c; }\n"
           << "    table { border-collapse: collapse; width: 100%; margin: 12px 0 20px 0; }\n"
           << "    th, td { border: 1px solid #d9e2ec; padding: 8px; text-align: left; }\n"
           << "    th { background: #f5f7fa; }\n"
           << "    td.numeric { text-align: right; font-variant-numeric: tabular-nums; }\n"
           << "    code { background: #f0f4f8; padding: 2px 4px; border-radius: 4px; }\n"
           << "    ul, ol { margin-top: 6px; }\n"
           << "  </style>\n"
           << "</head>\n"
           << "<body>\n"
           << "  <h1>LabOps Run Report</h1>\n"
           << "  <p class=\"meta\">Static triage report generated by LabOps (no JavaScript "
              "required).</p>\n"
           << "  <p><span class=\"status " << StatusCssClass(thresholds_passed) << "\">"
           << StatusLabel(thresholds_passed) << "</span></p>\n"
           << "\n"
           << "  <h2>Run Identity</h2>\n"
           << "  <table aria-label=\"run identity\">\n"
           << "    <thead><tr><th>Field</th><th>Value</th></tr></thead>\n"
           << "    <tbody>\n"
           << "      <tr><td>run_id</td><td><code>" << EscapeHtml(run_info.run_id)
           << "</code></td></tr>\n"
           << "      <tr><td>scenario_id</td><td><code>" << EscapeHtml(run_info.config.scenario_id)
           << "</code></td></tr>\n"
           << "      <tr><td>backend</td><td><code>" << EscapeHtml(run_info.config.backend)
           << "</code></td></tr>\n"
           << "      <tr><td>seed</td><td class=\"numeric\">" << run_info.config.seed
           << "</td></tr>\n"
           << "      <tr><td>duration_ms</td><td class=\"numeric\">"
           << run_info.config.duration.count() << "</td></tr>\n"
           << "      <tr><td>started_at_utc</td><td><code>"
           << EscapeHtml(core::FormatUtcTimestamp(run_info.timestamps.started_at))
           << "</code></td></tr>\n"
           << "      <tr><td>finished_at_utc</td><td><code>"
           << EscapeHtml(core::FormatUtcTimestamp(run_info.timestamps.finished_at))
           << "</code></td></tr>\n"
           << "    </tbody>\n"
           << "  </table>\n"
           << "\n"
           << "  <h2>Key Metrics</h2>\n"
           << "  <table aria-label=\"key metrics\">\n"
           << "    <thead><tr><th>Metric</th><th>Value</th><th>Unit</th></tr></thead>\n"
           << "    <tbody>\n"
           << "      <tr><td>configured_fps</td><td class=\"numeric\">" << configured_fps
           << "</td><td>fps</td></tr>\n"
           << "      <tr><td>avg_fps</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.avg_fps, 3) << "</td><td>fps</td></tr>\n"
           << "      <tr><td>frames_total</td><td class=\"numeric\">" << report.frames_total
           << "</td><td>count</td></tr>\n"
           << "      <tr><td>received_frames_total</td><td class=\"numeric\">"
           << report.received_frames_total << "</td><td>count</td></tr>\n"
           << "      <tr><td>dropped_frames_total</td><td class=\"numeric\">"
           << report.dropped_frames_total << "</td><td>count</td></tr>\n"
           << "      <tr><td>dropped_generic_frames_total</td><td class=\"numeric\">"
           << report.dropped_generic_frames_total << "</td><td>count</td></tr>\n"
           << "      <tr><td>timeout_frames_total</td><td class=\"numeric\">"
           << report.timeout_frames_total << "</td><td>count</td></tr>\n"
           << "      <tr><td>incomplete_frames_total</td><td class=\"numeric\">"
           << report.incomplete_frames_total << "</td><td>count</td></tr>\n"
           << "      <tr><td>drop_rate_percent</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.drop_rate_percent, 3) << "</td><td>%</td></tr>\n"
           << "      <tr><td>generic_drop_rate_percent</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.generic_drop_rate_percent, 3)
           << "</td><td>%</td></tr>\n"
           << "      <tr><td>timeout_rate_percent</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.timeout_rate_percent, 3) << "</td><td>%</td></tr>\n"
           << "      <tr><td>incomplete_rate_percent</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.incomplete_rate_percent, 3) << "</td><td>%</td></tr>\n"
           << "      <tr><td>inter_frame_interval_p95_us</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.inter_frame_interval_us.p95_us, 3)
           << "</td><td>us</td></tr>\n"
           << "      <tr><td>inter_frame_jitter_p95_us</td><td class=\"numeric\">"
           << core::FormatFixedDouble(report.inter_frame_jitter_us.p95_us, 3)
           << "</td><td>us</td></tr>\n"
           << "    </tbody>\n"
           << "  </table>\n"
           << "\n"
           << "  <h2>Diffs (Actual vs Expected)</h2>\n"
           << "  <table aria-label=\"metric deltas\">\n"
           << "    "
              "<thead><tr><th>Metric</th><th>Actual</th><th>Expected</th><th>Delta</th><th>Unit</"
              "th></tr></thead>\n"
           << "    <tbody>\n";

  for (const auto& row : delta_rows) {
    out_file << "      <tr><td>" << EscapeHtml(row.metric) << "</td><td class=\"numeric\">"
             << core::FormatFixedDouble(row.actual, 3) << "</td><td class=\"numeric\">"
             << core::FormatFixedDouble(row.expected, 3) << "</td><td class=\"numeric\">"
             << FormatSignedDouble(row.delta, 3) << "</td><td>" << EscapeHtml(row.unit)
             << "</td></tr>\n";
  }

  out_file << "    </tbody>\n"
           << "  </table>\n"
           << "\n"
           << "  <h2>Rolling FPS Samples</h2>\n"
           << "  <table aria-label=\"rolling fps samples\">\n"
           << "    "
              "<thead><tr><th>window_end_epoch_ms</th><th>frames_in_window</th><th>fps</th></tr></"
              "thead>\n"
           << "    <tbody>\n";

  for (const auto& sample : report.rolling_samples) {
    const auto window_end_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(sample.window_end.time_since_epoch())
            .count();
    out_file << "      <tr><td class=\"numeric\">" << window_end_ms << "</td><td class=\"numeric\">"
             << sample.frames_in_window << "</td><td class=\"numeric\">"
             << core::FormatFixedDouble(sample.fps, 6) << "</td></tr>\n";
  }

  out_file << "    </tbody>\n"
           << "  </table>\n"
           << "\n"
           << "  <h2>Threshold Checks</h2>\n";

  if (thresholds_passed) {
    out_file << "  <p>All configured thresholds passed.</p>\n";
  } else {
    out_file << "  <ul>\n";
    for (const auto& failure : threshold_failures) {
      out_file << "    <li>" << EscapeHtml(failure) << "</li>\n";
    }
    out_file << "  </ul>\n";
  }

  out_file << "\n"
           << "  <h2>Top Anomalies</h2>\n";

  if (top_anomalies.empty()) {
    out_file << "  <p>No notable anomalies detected.</p>\n";
  } else {
    out_file << "  <ol>\n";
    for (const auto& anomaly : top_anomalies) {
      out_file << "    <li>" << EscapeHtml(anomaly) << "</li>\n";
    }
    out_file << "  </ol>\n";
  }

  out_file << "</body>\n"
           << "</html>\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
