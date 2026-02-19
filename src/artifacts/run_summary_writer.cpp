#include "artifacts/run_summary_writer.hpp"

#include "core/time_utils.hpp"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

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

void WriteDeviceSelectionSection(std::ofstream& out_file, const core::schema::RunInfo& run_info) {
  if (!run_info.real_device.has_value() && !run_info.webcam_device.has_value()) {
    return;
  }

  out_file << "## Device Selection\n\n";
  if (run_info.real_device.has_value()) {
    const core::schema::RealDeviceMetadata& real = run_info.real_device.value();
    out_file << "- backend_device_type: `real`\n";
    out_file << "- model: `" << real.model << "`\n";
    out_file << "- serial: `" << real.serial << "`\n";
    out_file << "- transport: `" << real.transport << "`\n";
    if (real.user_id.has_value()) {
      out_file << "- user_id: `" << real.user_id.value() << "`\n";
    }
    if (real.firmware_version.has_value()) {
      out_file << "- firmware_version: `" << real.firmware_version.value() << "`\n";
    }
    if (real.sdk_version.has_value()) {
      out_file << "- sdk_version: `" << real.sdk_version.value() << "`\n";
    }
    out_file << '\n';
  }

  if (run_info.webcam_device.has_value()) {
    const core::schema::WebcamDeviceMetadata& webcam = run_info.webcam_device.value();
    out_file << "- backend_device_type: `webcam`\n";
    out_file << "- webcam_device_id: `" << webcam.device_id << "`\n";
    out_file << "- webcam_friendly_name: `" << webcam.friendly_name << "`\n";
    if (webcam.bus_info.has_value()) {
      out_file << "- webcam_bus_info: `" << webcam.bus_info.value() << "`\n";
    }
    if (webcam.selector_text.has_value()) {
      out_file << "- webcam_selector: `" << webcam.selector_text.value() << "`\n";
    }
    if (webcam.selection_rule.has_value()) {
      out_file << "- webcam_selection_rule: `" << webcam.selection_rule.value() << "`\n";
    }
    if (webcam.discovered_index.has_value()) {
      out_file << "- webcam_index: `" << webcam.discovered_index.value() << "`\n";
    }
    out_file << '\n';
  }
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

bool WriteRunSummaryMarkdown(const core::schema::RunInfo& run_info,
                             const metrics::FpsReport& report, const std::uint32_t configured_fps,
                             const bool thresholds_passed,
                             const std::vector<std::string>& threshold_failures,
                             const std::vector<std::string>& top_anomalies,
                             const std::optional<NetemCommandSuggestions>& netem_suggestions,
                             const fs::path& output_dir, fs::path& written_path,
                             std::string& error) {
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
  out_file << "- started_at_utc: `" << core::FormatUtcTimestamp(run_info.timestamps.started_at)
           << "`\n";
  out_file << "- finished_at_utc: `" << core::FormatUtcTimestamp(run_info.timestamps.finished_at)
           << "`\n\n";

  out_file << "## Key Metrics\n\n";
  out_file << "| Metric | Value |\n";
  out_file << "| --- | --- |\n";
  out_file << "| configured_fps | " << configured_fps << " |\n";
  out_file << "| avg_fps | " << core::FormatFixedDouble(report.avg_fps, 3) << " |\n";
  out_file << "| frames_total | " << report.frames_total << " |\n";
  out_file << "| received_frames_total | " << report.received_frames_total << " |\n";
  out_file << "| dropped_frames_total | " << report.dropped_frames_total << " |\n";
  out_file << "| dropped_generic_frames_total | " << report.dropped_generic_frames_total << " |\n";
  out_file << "| timeout_frames_total | " << report.timeout_frames_total << " |\n";
  out_file << "| incomplete_frames_total | " << report.incomplete_frames_total << " |\n";
  out_file << "| drop_rate_percent | " << core::FormatFixedDouble(report.drop_rate_percent, 3)
           << " |\n";
  out_file << "| generic_drop_rate_percent | "
           << core::FormatFixedDouble(report.generic_drop_rate_percent, 3) << " |\n";
  out_file << "| timeout_rate_percent | " << core::FormatFixedDouble(report.timeout_rate_percent, 3)
           << " |\n";
  out_file << "| incomplete_rate_percent | "
           << core::FormatFixedDouble(report.incomplete_rate_percent, 3) << " |\n";
  out_file << "| inter_frame_interval_p95_us | "
           << core::FormatFixedDouble(report.inter_frame_interval_us.p95_us, 3) << " |\n";
  out_file << "| inter_frame_jitter_p95_us | "
           << core::FormatFixedDouble(report.inter_frame_jitter_us.p95_us, 3) << " |\n\n";

  WriteDeviceSelectionSection(out_file, run_info);
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
