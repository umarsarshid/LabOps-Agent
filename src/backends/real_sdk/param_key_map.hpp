#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::real_sdk {

// Data-driven mapping from generic LabOps parameter keys to vendor SDK node
// names. This keeps evolving key-node relationships outside core runtime logic.
struct ParamKeyMap {
  std::map<std::string, std::string, std::less<>> generic_to_node;

  bool Has(std::string_view generic_key) const;
  bool Resolve(std::string_view generic_key, std::string& node_name) const;
  std::vector<std::string> ListGenericKeys() const;
};

// Parse a JSON object where both keys and values are strings.
//
// Expected shape:
// {
//   "exposure": "ExposureTime",
//   "gain": "Gain"
// }
bool LoadParamKeyMapFromText(std::string_view json_text, ParamKeyMap& map, std::string& error);

// Load param-key mapping from a JSON file.
bool LoadParamKeyMapFromFile(const std::filesystem::path& path, ParamKeyMap& map,
                             std::string& error);

// Resolve the default on-disk mapping path.
//
// Lookup order:
// 1) `LABOPS_PARAM_KEY_MAP` env var, if set
// 2) nearest `src/backends/real_sdk/maps/param_key_map.json` by walking up from cwd
std::filesystem::path ResolveDefaultParamKeyMapPath();

} // namespace labops::backends::real_sdk
