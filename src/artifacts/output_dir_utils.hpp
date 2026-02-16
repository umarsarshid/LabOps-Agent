#ifndef LABOPS_ARTIFACTS_OUTPUT_DIR_UTILS_HPP_
#define LABOPS_ARTIFACTS_OUTPUT_DIR_UTILS_HPP_

#include <filesystem>
#include <string>
#include <system_error>

namespace labops::artifacts {

// Centralized output-dir creation guard used by artifact writers.
// Shared error text keeps CLI and tests consistent across artifact types.
inline bool EnsureOutputDir(const std::filesystem::path& output_dir, std::string& error) {
  if (output_dir.empty()) {
    error = "output directory cannot be empty";
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + output_dir.string() + "': " + ec.message();
    return false;
  }
  return true;
}

} // namespace labops::artifacts

#endif // LABOPS_ARTIFACTS_OUTPUT_DIR_UTILS_HPP_
