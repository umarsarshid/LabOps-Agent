#include "metrics/csv_writer.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::metrics {

namespace {

std::int64_t ToEpochMillis(std::chrono::system_clock::time_point ts) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
}

} // namespace

bool WriteFpsMetricsCsv(const FpsReport& report, const fs::path& output_dir,
                        fs::path& written_path, std::string& error) {
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

  for (const auto& sample : report.rolling_samples) {
    out_file << "rolling_fps,"
             << ToEpochMillis(sample.window_end) << ","
             << report.rolling_window.count() << ","
             << sample.frames_in_window << ","
             << sample.fps << "\n";
  }

  // Timing/jitter stats are emitted as dedicated metric rows so downstream
  // tools can compare scenario quality without parsing event-level traces.
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

} // namespace labops::metrics
