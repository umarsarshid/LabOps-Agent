#pragma once

#include "backends/camera_backend.hpp"
#include "backends/real_sdk/node_map_adapter.hpp"
#include "backends/real_sdk/param_key_map.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::real_sdk {

// Controls how unsupported parameters are handled while applying a scenario to
// a real backend.
enum class ParamApplyMode {
  kStrict = 0,
  kBestEffort,
};

struct ApplyParamInput {
  std::string generic_key;
  std::string requested_value;
};

struct UnsupportedParam {
  std::string generic_key;
  std::string requested_value;
  std::string reason;
};

struct AppliedParam {
  std::string generic_key;
  std::string node_name;
  std::string requested_value;
  std::string applied_value;
  bool adjusted = false;
  std::string adjustment_reason;
};

// Per-setting readback row captured after apply attempt.
//
// This is the canonical evidence record for "what was requested vs what the
// backend/node model actually holds", including unsupported and failed-apply
// cases.
struct ReadbackRow {
  std::string generic_key;
  std::string node_name;
  std::string requested_value;
  std::string actual_value;
  bool supported = false;
  bool applied = false;
  bool adjusted = false;
  std::string reason;
};

struct ApplyParamsResult {
  std::vector<AppliedParam> applied;
  std::vector<UnsupportedParam> unsupported;
  std::vector<ReadbackRow> readback_rows;
};

const char* ToString(ParamApplyMode mode);

bool ParseParamApplyMode(std::string_view raw_mode, ParamApplyMode& mode, std::string& error);

// Creates the deterministic in-memory node adapter used by the current
// non-proprietary real-backend path.
std::unique_ptr<INodeMapAdapter> CreateDefaultNodeMapAdapter();

// Applies generic scenario parameters to the backend by:
// 1) resolving generic key -> SDK node name via ParamKeyMap
// 2) validating/coercing values against NodeMapAdapter contracts
// 3) setting backend params using resolved SDK node names
//
// Strict mode: fails on first unsupported setting.
// Best-effort mode: records unsupported settings and continues.
bool ApplyParams(ICameraBackend& backend, const ParamKeyMap& key_map, INodeMapAdapter& node_adapter,
                 const std::vector<ApplyParamInput>& params, ParamApplyMode mode,
                 ApplyParamsResult& result, std::string& error);

} // namespace labops::backends::real_sdk
