#pragma once

#include "events/event_model.hpp"

#include <filesystem>
#include <string>

namespace labops::events {

// Appends one JSON-serialized event per line to `<output_dir>/events.jsonl`.
//
// Contract:
// - Creates `output_dir` if needed.
// - Opens `events.jsonl` in append mode.
// - Writes exactly one line per call.
// - Returns false with `error` populated on failure.
bool AppendEventJsonl(const Event& event, const std::filesystem::path& output_dir,
                      std::filesystem::path& written_path, std::string& error);

} // namespace labops::events
