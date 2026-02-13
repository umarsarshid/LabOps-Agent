#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace labops::artifacts {

// Writes bundle_manifest.json for a completed run bundle.
//
// Contract:
// - `bundle_dir` is the root run bundle directory (`<out>/<run_id>`).
// - `artifact_paths` must list files to include in the manifest.
// - Each listed file is hashed with FNV-1a 64-bit and emitted with size.
// - Writes `<bundle_dir>/bundle_manifest.json`.
// - Returns true on success and populates `written_path`.
// - Returns false on failure and populates `error`.
bool WriteBundleManifestJson(const std::filesystem::path& bundle_dir,
                             const std::vector<std::filesystem::path>& artifact_paths,
                             std::filesystem::path& written_path,
                             std::string& error);

} // namespace labops::artifacts
