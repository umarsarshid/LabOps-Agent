#include "artifacts/metrics_writer.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

std::int64_t ToEpochMillis(std::chrono::system_clock::time_point ts) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
}

std::string FormatDouble(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
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

void WriteTimingStatsJsonObject(std::ofstream& out_file, const std::string& key,
                                const metrics::TimingStatsUs& stats) {
  out_file << "  \"" << key << "\":{"
           << "\"sample_count\":" << stats.sample_count << ","
           << "\"min_us\":" << FormatDouble(stats.min_us) << ","
           << "\"avg_us\":" << FormatDouble(stats.avg_us) << ","
           << "\"p95_us\":" << FormatDouble(stats.p95_us) << "}";
}

} // namespace

bool WriteMetricsCsv(const metrics::FpsReport& report, const fs::path& output_dir,
                     fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "metrics.csv";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out_file << "metric,window_end_ms,window_ms,frames,fps\n";
  out_file << std::fixed << std::setprecision(6);
  out_file << "avg_fps,,"
           << report.avg_window.count() << ","
           << report.received_frames_total << ","
           << report.avg_fps << "\n";

  out_file << "drops_total,,,"
           << report.frames_total << ","
           << report.dropped_frames_total << "\n";
  out_file << "drop_rate_percent,,,"
           << report.frames_total << ","
           << report.drop_rate_percent << "\n";

  for (const auto& sample : report.rolling_samples) {
    out_file << "rolling_fps,"
             << ToEpochMillis(sample.window_end) << ","
             << report.rolling_window.count() << ","
             << sample.frames_in_window << ","
             << sample.fps << "\n";
  }

  out_file << "inter_frame_interval_min_us,,,"
           << report.inter_frame_interval_us.sample_count << ","
           << report.inter_frame_interval_us.min_us << "\n";
  out_file << "inter_frame_interval_avg_us,,,"
           << report.inter_frame_interval_us.sample_count << ","
           << report.inter_frame_interval_us.avg_us << "\n";
  out_file << "inter_frame_interval_p95_us,,,"
           << report.inter_frame_interval_us.sample_count << ","
           << report.inter_frame_interval_us.p95_us << "\n";

  out_file << "inter_frame_jitter_min_us,,,"
           << report.inter_frame_jitter_us.sample_count << ","
           << report.inter_frame_jitter_us.min_us << "\n";
  out_file << "inter_frame_jitter_avg_us,,,"
           << report.inter_frame_jitter_us.sample_count << ","
           << report.inter_frame_jitter_us.avg_us << "\n";
  out_file << "inter_frame_jitter_p95_us,,,"
           << report.inter_frame_jitter_us.sample_count << ","
           << report.inter_frame_jitter_us.p95_us << "\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

bool WriteMetricsJson(const metrics::FpsReport& report, const fs::path& output_dir,
                      fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "metrics.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out_file << "{\n"
           << "  \"avg_window_ms\":" << report.avg_window.count() << ",\n"
           << "  \"rolling_window_ms\":" << report.rolling_window.count() << ",\n"
           << "  \"frames_total\":" << report.frames_total << ",\n"
           << "  \"received_frames_total\":" << report.received_frames_total << ",\n"
           << "  \"dropped_frames_total\":" << report.dropped_frames_total << ",\n"
           << "  \"drop_rate_percent\":" << FormatDouble(report.drop_rate_percent) << ",\n"
           << "  \"avg_fps\":" << FormatDouble(report.avg_fps) << ",\n";

  WriteTimingStatsJsonObject(out_file, "inter_frame_interval_us", report.inter_frame_interval_us);
  out_file << ",\n";
  WriteTimingStatsJsonObject(out_file, "inter_frame_jitter_us", report.inter_frame_jitter_us);
  out_file << ",\n";

  out_file << "  \"rolling_fps\":[";
  for (std::size_t i = 0; i < report.rolling_samples.size(); ++i) {
    const auto& sample = report.rolling_samples[i];
    if (i != 0U) {
      out_file << ",";
    }
    out_file << "{\"window_end_ms\":" << ToEpochMillis(sample.window_end)
             << ",\"frames_in_window\":" << sample.frames_in_window
             << ",\"fps\":" << FormatDouble(sample.fps) << "}";
  }
  out_file << "]\n"
           << "}\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
