#include "artifacts/hostprobe_writer.hpp"

#include "core/fs_utils.hpp"

#include <system_error>
#include <utility>

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

std::string BuildRawCommandText(const hostprobe::NicCommandCapture& capture) {
  std::string text;
  text.reserve(capture.output.size() + 128U);
  text += "# command: " + capture.command + "\n";
  text += "# exit_code: " + std::to_string(capture.exit_code) + "\n";
  text += std::string("# command_available: ") + (capture.command_available ? "true" : "false") +
          "\n\n";
  if (capture.output.empty()) {
    text += "<no output>\n";
  } else {
    text += capture.output;
    if (text.back() != '\n') {
      text.push_back('\n');
    }
  }
  return text;
}

bool WriteTextFile(const fs::path& path, const std::string& text, std::string& error) {
  if (!core::WriteTextFileAtomic(path, text, error)) {
    error = "failed while writing output file '" + path.string() + "' (" + error + ")";
    return false;
  }
  return true;
}

} // namespace

bool WriteHostProbeJson(const hostprobe::HostProbeSnapshot& snapshot, const fs::path& output_dir,
                        fs::path& written_path, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_path = output_dir / "hostprobe.json";
  std::string json = hostprobe::ToJson(snapshot);
  json.push_back('\n');
  if (!core::WriteTextFileAtomic(written_path, json, error)) {
    error = "failed while writing output file '" + written_path.string() + "' (" + error + ")";
    return false;
  }

  return true;
}

bool WriteHostProbeRawCommandOutputs(const std::vector<hostprobe::NicCommandCapture>& captures,
                                     const fs::path& output_dir,
                                     std::vector<fs::path>& written_paths, std::string& error) {
  if (!EnsureOutputDir(output_dir, error)) {
    return false;
  }

  written_paths.clear();

  std::vector<hostprobe::NicCommandCapture> normalized = captures;
  if (normalized.empty()) {
    hostprobe::NicCommandCapture placeholder;
    placeholder.file_name = "nic_probe_unavailable.txt";
    placeholder.command = "nic_probe_unavailable";
    placeholder.exit_code = 127;
    placeholder.command_available = false;
    placeholder.output = "No NIC command captures were produced by host probe.\n";
    normalized.push_back(std::move(placeholder));
  }

  for (const auto& capture : normalized) {
    if (capture.file_name.empty()) {
      error = "NIC command capture file_name cannot be empty";
      return false;
    }

    const fs::path output_path = output_dir / capture.file_name;
    if (!WriteTextFile(output_path, BuildRawCommandText(capture), error)) {
      return false;
    }
    written_paths.push_back(output_path);
  }

  return true;
}

} // namespace labops::artifacts
