#pragma once

#include <filesystem>
#include <string>

namespace labops::artifacts {

// Generates a KB draft markdown document from a run folder by reading
// `engineer_packet.md` and projecting it into a publish-friendly template.
//
// Contract:
// - `run_dir` must point to a run/evidence directory.
// - `engineer_packet.md` is expected under `run_dir` or `run_dir/packet/`.
// - default output is typically `<run_dir>/kb_draft.md` (resolved by caller).
// - returns false with actionable `error` when required inputs are missing.
bool WriteKbDraftFromRunFolder(const std::filesystem::path& run_dir,
                               const std::filesystem::path& output_path,
                               std::filesystem::path& written_path, std::string& error);

} // namespace labops::artifacts
