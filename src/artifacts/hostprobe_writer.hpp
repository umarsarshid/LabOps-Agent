#pragma once

#include "hostprobe/system_probe.hpp"

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Writes the canonical host probe artifact for a run bundle.
//
// Contract:
// - creates `output_dir` when missing.
// - writes `<output_dir>/hostprobe.json`.
// - returns false and sets `error` on failures.
bool WriteHostProbeJson(const hostprobe::HostProbeSnapshot& snapshot,
                        const std::filesystem::path& output_dir,
                        std::filesystem::path& written_path,
                        std::string& error);

} // namespace labops::artifacts
