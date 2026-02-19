#include "artifacts/run_writer.hpp"

#include "core/fs_utils.hpp"

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

  written_path = output_dir / "run.json";
  // Append a newline to keep files shell-friendly (`cat`, `tail`, diffs).
  std::string json = core::schema::ToJson(run_info);
  json.push_back('\n');
  if (!core::WriteTextFileAtomic(written_path, json, error)) {
    error = "failed while writing output file '" + written_path.string() + "' (" + error + ")";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
