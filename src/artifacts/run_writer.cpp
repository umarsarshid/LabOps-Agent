#include "artifacts/run_writer.hpp"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

bool WriteRunJson(const core::schema::RunInfo& run_info, const fs::path& output_dir,
                  fs::path& written_path, std::string& error) {
  // Treat missing/empty output target as a caller contract violation so the
  // CLI can surface a clear, actionable message.
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  // Create the full output path up front so first-time runs and CI jobs can
  // write artifacts without requiring pre-created directories.
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }

  written_path = output_dir / "run.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  // Append a newline to keep files shell-friendly (`cat`, `tail`, diffs).
  out_file << core::schema::ToJson(run_info) << '\n';
  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
