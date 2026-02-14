#include "artifacts/run_summary_writer.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

std::string FormatDouble(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
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

void WriteThresholdSection(std::ofstream& out_file, bool thresholds_passed,
                           const std::vector<std::string>& threshold_failures) {
  out_file << "## Threshold Checks\n\n";
  if (thresholds_passed) {
    out_file << "- All configured thresholds passed.\n\n";
    return;
  }

  out_file << "- Threshold violations: " << threshold_failures.size() << '\n';
  for (const auto& failure : threshold_failures) {
    out_file << "- " << failure << '\n';
  }
  out_file << '\n';
}

void WriteAnomaliesSection(std::ofstream& out_file, const std::vector<std::string>& top_anomalies) {
  out_file << "## Top Anomalies\n\n";
  if (top_anomalies.empty()) {
    out_file << "1. No notable anomalies detected.\n\n";
    return;
  }

  std::size_t index = 1;
  for (const auto& anomaly : top_anomalies) {
    out_file << index << ". " << anomaly << '\n';
    ++index;
  }
  out_file << '\n';
}

void WriteNetemCommandSection(std::ofstream& out_file,
                              const std::optional<NetemCommandSuggestions>& netem_suggestions) {
  if (!netem_suggestions.has_value()) {
    return;
  }

  const NetemCommandSuggestions& netem = netem_suggestions.value();
  out_file << "## Netem Commands (Manual)\n\n";
  out_file << "- profile_id: `" << netem.profile_id << "`\n";
  out_file << "- profile_path: `" << netem.profile_path.string() << "`\n";
  out_file << "- note: " << netem.safety_note << "\n\n";
  out_file << "```bash\n";
  out_file << netem.apply_command << '\n';
  out_file << netem.show_command << '\n';
  out_file << netem.teardown_command << '\n';
  out_file << "```\n\n";
}

} // namespace

bool WriteRunSummaryMarkdown(const core::schema::RunInfo& run_info, const metrics::FpsReport& report,
                             const std::uint32_t configured_fps, const bool thresholds_passed,
                             const std::vector<std::string>& threshold_failures,
                             const std::vector<std::string>& top_anomalies,
                             const std::optional<NetemCommandSuggestions>& netem_suggestions,
                             const fs::path& output_dir, fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "summary.md";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  // Keep this intentionally concise so humans can scan it quickly during triage.
  out_file << "# Run Summary\n\n";
  out_file << "## Status\n\n";
  out_file << "**" << (thresholds_passed ? "PASS" : "FAIL") << "**\n\n";

  out_file << "## Run Identity\n\n";
  out_file << "- run_id: `" << run_info.run_id << "`\n";
  out_file << "- scenario_id: `" << run_info.config.scenario_id << "`\n";
  out_file << "- backend: `" << run_info.config.backend << "`\n";
  out_file << "- seed: `" << run_info.config.seed << "`\n";
  out_file << "- duration_ms: `" << run_info.config.duration.count() << "`\n";
  out_file << "- started_at_utc: `" << FormatUtcTimestamp(run_info.timestamps.started_at) << "`\n";
  out_file << "- finished_at_utc: `" << FormatUtcTimestamp(run_info.timestamps.finished_at) << "`\n\n";

  out_file << "## Key Metrics\n\n";
  out_file << "| Metric | Value |\n";
  out_file << "| --- | --- |\n";
  out_file << "| configured_fps | " << configured_fps << " |\n";
  out_file << "| avg_fps | " << FormatDouble(report.avg_fps, 3) << " |\n";
  out_file << "| frames_total | " << report.frames_total << " |\n";
  out_file << "| received_frames_total | " << report.received_frames_total << " |\n";
  out_file << "| dropped_frames_total | " << report.dropped_frames_total << " |\n";
  out_file << "| drop_rate_percent | " << FormatDouble(report.drop_rate_percent, 3) << " |\n";
  out_file << "| inter_frame_interval_p95_us | "
           << FormatDouble(report.inter_frame_interval_us.p95_us, 3) << " |\n";
  out_file << "| inter_frame_jitter_p95_us | "
           << FormatDouble(report.inter_frame_jitter_us.p95_us, 3) << " |\n\n";

  WriteThresholdSection(out_file, thresholds_passed, threshold_failures);
  WriteAnomaliesSection(out_file, top_anomalies);
  WriteNetemCommandSection(out_file, netem_suggestions);

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
