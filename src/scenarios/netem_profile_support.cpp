#include "scenarios/netem_profile_support.hpp"

#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::scenarios {

bool IsLowercaseSlug(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const char c : value) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool ResolveNetemProfilePath(const fs::path& scenario_path, std::string_view profile_id,
                             fs::path& resolved_path) {
  resolved_path.clear();
  if (scenario_path.empty() || profile_id.empty()) {
    return false;
  }

  std::error_code ec;
  const fs::path scenario_absolute = fs::absolute(scenario_path, ec);
  if (ec) {
    return false;
  }

  fs::path cursor = scenario_absolute.parent_path();
  while (!cursor.empty()) {
    const fs::path candidate =
        cursor / "tools" / "netem_profiles" / (std::string(profile_id) + ".json");
    std::error_code exists_ec;
    if (fs::exists(candidate, exists_ec) && !exists_ec &&
        fs::is_regular_file(candidate, exists_ec) && !exists_ec) {
      resolved_path = candidate;
      return true;
    }

    const fs::path parent = cursor.parent_path();
    if (parent.empty() || parent == cursor) {
      break;
    }
    cursor = parent;
  }

  return false;
}

} // namespace labops::scenarios
