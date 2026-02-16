#pragma once

#include <filesystem>
#include <string_view>

namespace labops::scenarios {

// Shared slug contract used by scenario validation and runtime option parsing.
// Keeping this centralized prevents drift between `labops validate` and
// `labops run` when accepting profile identifiers.
bool IsLowercaseSlug(std::string_view value);

// Resolves `tools/netem_profiles/<profile_id>.json` by walking from the
// scenario file directory upward to the repo root.
//
// Returns true and fills `resolved_path` when found, otherwise returns false and
// clears `resolved_path`.
bool ResolveNetemProfilePath(const std::filesystem::path& scenario_path,
                             std::string_view profile_id, std::filesystem::path& resolved_path);

} // namespace labops::scenarios
