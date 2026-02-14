#pragma once

#include "hostprobe/system_probe.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace labops::artifacts {

// Writes the canonical host probe artifact for a run bundle.
//
// Contract:
// - creates `output_dir` when missing.
// - writes `<output_dir>/hostprobe.json`.
// - returns false and sets `error` on failures.
bool WriteHostProbeJson(const hostprobe::HostProbeSnapshot& snapshot,
                        const std::filesystem::path& output_dir,
                        std::filesystem::path& written_path, std::string& error);

// Writes raw NIC command captures as text artifacts.
//
// Contract:
// - creates `output_dir` when missing.
// - writes one `<output_dir>/<file_name>` per capture.
// - always writes at least one placeholder file if `captures` is empty.
// - returns false and sets `error` on filesystem/write failures.
bool WriteHostProbeRawCommandOutputs(const std::vector<hostprobe::NicCommandCapture>& captures,
                                     const std::filesystem::path& output_dir,
                                     std::vector<std::filesystem::path>& written_paths,
                                     std::string& error);

} // namespace labops::artifacts
