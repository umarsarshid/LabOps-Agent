#include "events/jsonl_writer.hpp"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::events {

bool AppendEventJsonl(const Event& event, const fs::path& output_dir, fs::path& written_path,
                      std::string& error) {
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  // Ensure first-time run directories and CI temp paths are writable without
  // requiring pre-created folders.
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }

  written_path = output_dir / "events.jsonl";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::app);
  if (!out_file) {
    error = "failed to open event log '" + written_path.string() + "' for append";
    return false;
  }

  out_file << ToJson(event) << '\n';
  if (!out_file) {
    error = "failed while writing event log '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::events
