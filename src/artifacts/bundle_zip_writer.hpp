#pragma once

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Writes an optional support zip for a completed run bundle.
//
// Contract:
// - `bundle_dir` must point to `<out>/<run_id>` and contain run artifacts.
// - Output path is `<out>/<run_id>.zip` (sibling of bundle directory).
// - Zip entries are stored with no compression for predictable behavior.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteBundleZip(const std::filesystem::path& bundle_dir, std::filesystem::path& written_path,
                    std::string& error);

} // namespace labops::artifacts
