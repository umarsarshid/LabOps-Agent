#include "backends/real_sdk/apply_params.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

namespace labops::backends::real_sdk {

namespace {

std::string Trim(std::string_view input) {
  std::size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
    ++begin;
  }

  std::size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
    --end;
  }

  return std::string(input.substr(begin, end - begin));
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string FormatDouble(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  std::string text = out.str();
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  if (text.empty()) {
    return "0";
  }
  return text;
}

std::string FormatRangeText(const NodeNumericRange& range) {
  const std::string min_text = range.min.has_value() ? FormatDouble(range.min.value()) : "-inf";
  const std::string max_text = range.max.has_value() ? FormatDouble(range.max.value()) : "+inf";
  return "[" + min_text + ", " + max_text + "]";
}

bool ParseBool(std::string_view raw, bool& parsed) {
  const std::string normalized = ToLower(Trim(raw));
  if (normalized == "true" || normalized == "1" || normalized == "on") {
    parsed = true;
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "off") {
    parsed = false;
    return true;
  }
  return false;
}

bool ParseInt64(std::string_view raw, std::int64_t& parsed) {
  const std::string text = Trim(raw);
  if (text.empty()) {
    return false;
  }

  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return false;
  }
  return true;
}

bool ParseDouble(std::string_view raw, double& parsed) {
  const std::string text = Trim(raw);
  if (text.empty()) {
    return false;
  }

  char* parse_end = nullptr;
  parsed = std::strtod(text.c_str(), &parse_end);
  if (parse_end == nullptr || *parse_end != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  return true;
}

bool ClampWithRange(double& value, const NodeNumericRange& range, std::string& reason) {
  const double requested = value;
  bool adjusted = false;

  if (range.min.has_value() && value < range.min.value()) {
    value = range.min.value();
    adjusted = true;
  }
  if (range.max.has_value() && value > range.max.value()) {
    value = range.max.value();
    adjusted = true;
  }

  if (!adjusted) {
    reason.clear();
    return false;
  }

  reason = "clamped from " + FormatDouble(requested) + " to " + FormatDouble(value) +
           " (allowed range " + FormatRangeText(range) + ")";
  return true;
}

std::optional<std::string> FindCaseInsensitiveEnumValue(const std::vector<std::string>& values,
                                                        std::string_view requested) {
  const std::string requested_lower = ToLower(std::string(requested));
  for (const std::string& value : values) {
    if (ToLower(value) == requested_lower) {
      return value;
    }
  }
  return std::nullopt;
}

std::string JoinEnumValues(const std::vector<std::string>& values) {
  if (values.empty()) {
    return "(none)";
  }

  std::string joined;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) {
      joined += ", ";
    }
    joined += values[i];
  }
  return joined;
}

using ValueTransformHook = bool (*)(const std::string& requested_value,
                                    const std::vector<std::string>& enum_values,
                                    std::string& transformed_value, bool& adjusted,
                                    std::string& adjustment_reason);

using ReadbackHook = void (*)(const AppliedParam& applied, ReadbackRow& readback_row);

struct ParamRule {
  std::string_view generic_key;
  NodeValueType expected_node_type = NodeValueType::kUnknown;
  bool has_expected_node_type = false;
  std::optional<NodeNumericRange> numeric_limits_hint;
  int apply_priority = 10;
  bool force_best_effort = false;
  ValueTransformHook transform_hook = nullptr;
  ReadbackHook readback_hook = nullptr;
};

bool TransformIdentityValue(const std::string& requested_value,
                            const std::vector<std::string>& /*enum_values*/,
                            std::string& transformed_value, bool& adjusted,
                            std::string& adjustment_reason) {
  transformed_value = requested_value;
  adjusted = false;
  adjustment_reason.clear();
  return true;
}

bool TransformEnumCaseInsensitive(const std::string& requested_value,
                                  const std::vector<std::string>& enum_values,
                                  std::string& transformed_value, bool& adjusted,
                                  std::string& adjustment_reason) {
  transformed_value = requested_value;
  adjusted = false;
  adjustment_reason.clear();
  if (enum_values.empty()) {
    return true;
  }

  const std::optional<std::string> canonical =
      FindCaseInsensitiveEnumValue(enum_values, requested_value);
  if (!canonical.has_value()) {
    return false;
  }

  transformed_value = canonical.value();
  if (canonical.value() != requested_value) {
    adjusted = true;
    adjustment_reason = "normalized enumeration value casing";
  }
  return true;
}

void ReadbackNoOp(const AppliedParam& /*applied*/, ReadbackRow& /*readback_row*/) {}

ParamRule MakeRule(std::string_view generic_key, NodeValueType expected_node_type,
                   bool has_expected_node_type, std::optional<NodeNumericRange> numeric_limits_hint,
                   int apply_priority, bool force_best_effort, ValueTransformHook transform_hook,
                   ReadbackHook readback_hook) {
  ParamRule rule;
  rule.generic_key = generic_key;
  rule.expected_node_type = expected_node_type;
  rule.has_expected_node_type = has_expected_node_type;
  rule.numeric_limits_hint = std::move(numeric_limits_hint);
  rule.apply_priority = apply_priority;
  rule.force_best_effort = force_best_effort;
  rule.transform_hook = transform_hook;
  rule.readback_hook = readback_hook;
  return rule;
}

std::optional<NodeNumericRange> MakeRangeHint(double min, double max) {
  return NodeNumericRange{
      .min = min,
      .max = max,
  };
}

const std::array<ParamRule, 14> kParamRules = {
    MakeRule("exposure", NodeValueType::kFloat64, true, MakeRangeHint(5.0, 10'000'000.0), 10, false,
             TransformIdentityValue, ReadbackNoOp),
    MakeRule("gain", NodeValueType::kFloat64, true, MakeRangeHint(0.0, 48.0), 10, false,
             TransformIdentityValue, ReadbackNoOp),
    MakeRule("pixel_format", NodeValueType::kEnumeration, true, std::nullopt, 10, false,
             TransformEnumCaseInsensitive, ReadbackNoOp),
    MakeRule("roi_width", NodeValueType::kInt64, true, MakeRangeHint(64.0, 4096.0), 0, false,
             TransformIdentityValue, ReadbackNoOp),
    MakeRule("roi_height", NodeValueType::kInt64, true, MakeRangeHint(64.0, 2160.0), 1, false,
             TransformIdentityValue, ReadbackNoOp),
    MakeRule("roi_offset_x", NodeValueType::kInt64, true, MakeRangeHint(0.0, 4095.0), 2, false,
             TransformIdentityValue, ReadbackNoOp),
    MakeRule("roi_offset_y", NodeValueType::kInt64, true, MakeRangeHint(0.0, 2159.0), 3, false,
             TransformIdentityValue, ReadbackNoOp),
    MakeRule("roi", NodeValueType::kString, true, std::nullopt, 10, false, TransformIdentityValue,
             ReadbackNoOp),
    MakeRule("packet_size_bytes", NodeValueType::kInt64, true, MakeRangeHint(576.0, 9000.0), 10,
             true, TransformIdentityValue, ReadbackNoOp),
    MakeRule("inter_packet_delay_us", NodeValueType::kInt64, true, MakeRangeHint(0.0, 100'000.0),
             10, true, TransformIdentityValue, ReadbackNoOp),
    MakeRule("trigger_mode", NodeValueType::kEnumeration, true, std::nullopt, 10, false,
             TransformEnumCaseInsensitive, ReadbackNoOp),
    MakeRule("trigger_source", NodeValueType::kEnumeration, true, std::nullopt, 10, false,
             TransformEnumCaseInsensitive, ReadbackNoOp),
    MakeRule("trigger_activation", NodeValueType::kEnumeration, true, std::nullopt, 10, false,
             TransformEnumCaseInsensitive, ReadbackNoOp),
    MakeRule("frame_rate", NodeValueType::kFloat64, true, MakeRangeHint(1.0, 240.0), 10, true,
             TransformIdentityValue, ReadbackNoOp),
};

const ParamRule& ResolveParamRule(std::string_view generic_key) {
  for (const ParamRule& rule : kParamRules) {
    if (rule.generic_key == generic_key) {
      return rule;
    }
  }

  static const ParamRule kDefaultRule = MakeRule("", NodeValueType::kUnknown, false, std::nullopt,
                                                 10, false, TransformIdentityValue, ReadbackNoOp);
  return kDefaultRule;
}

std::vector<ApplyParamInput> OrderApplyInputs(const std::vector<ApplyParamInput>& params) {
  std::vector<ApplyParamInput> ordered = params;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const ApplyParamInput& lhs, const ApplyParamInput& rhs) {
                     return ResolveParamRule(lhs.generic_key).apply_priority <
                            ResolveParamRule(rhs.generic_key).apply_priority;
                   });
  return ordered;
}

ParamApplyMode ResolveModeForRule(const ParamRule& rule, ParamApplyMode default_mode) {
  if (rule.force_best_effort) {
    return ParamApplyMode::kBestEffort;
  }
  return default_mode;
}

bool TryReadNodeValueAsString(INodeMapAdapter& node_adapter, std::string_view node_name,
                              std::string& actual_value, std::string& error) {
  actual_value.clear();
  error.clear();

  switch (node_adapter.GetType(node_name)) {
  case NodeValueType::kBool: {
    bool value = false;
    if (!node_adapter.TryGetBool(node_name, value)) {
      error = "failed to read bool value";
      return false;
    }
    actual_value = value ? "true" : "false";
    return true;
  }
  case NodeValueType::kInt64: {
    std::int64_t value = 0;
    if (!node_adapter.TryGetInt64(node_name, value)) {
      error = "failed to read integer value";
      return false;
    }
    actual_value = std::to_string(value);
    return true;
  }
  case NodeValueType::kFloat64: {
    double value = 0.0;
    if (!node_adapter.TryGetFloat64(node_name, value)) {
      error = "failed to read float value";
      return false;
    }
    actual_value = FormatDouble(value);
    return true;
  }
  case NodeValueType::kString:
  case NodeValueType::kEnumeration: {
    std::string value;
    if (!node_adapter.TryGetString(node_name, value)) {
      error = "failed to read string value";
      return false;
    }
    actual_value = value;
    return true;
  }
  case NodeValueType::kUnknown:
  default:
    error = "node value type is unknown";
    return false;
  }
}

// Centralized unsupported-parameter handling so strict and best-effort modes
// follow one code path. This avoids branch drift across parse/map/set stages.
bool RecordUnsupportedParameter(const std::string& generic_key, const std::string& requested_value,
                                const std::optional<std::string>& node_name, bool supported,
                                const std::string& reason, ParamApplyMode mode,
                                ApplyParamsResult& result, std::string& error) {
  ReadbackRow readback_row{
      .generic_key = generic_key,
      .requested_value = requested_value,
      .supported = supported,
      .applied = false,
      .reason = reason,
  };
  if (node_name.has_value()) {
    readback_row.node_name = node_name.value();
  }
  result.readback_rows.push_back(std::move(readback_row));

  result.unsupported.push_back(UnsupportedParam{
      .generic_key = generic_key,
      .requested_value = requested_value,
      .reason = reason,
  });

  if (mode == ParamApplyMode::kStrict) {
    error = "unsupported parameter '" + generic_key + "': " + reason;
    return false;
  }
  return true;
}

struct PreparedNodeWrite {
  std::string backend_value;
  bool adjusted = false;
  std::string adjustment_reason;
  std::string unsupported_reason;
};

bool PrepareBoolWrite(INodeMapAdapter& node_adapter, std::string_view node_name,
                      std::string_view requested_value, PreparedNodeWrite& prepared,
                      std::string& write_error) {
  prepared = PreparedNodeWrite{};
  bool parsed = false;
  if (!ParseBool(requested_value, parsed)) {
    prepared.unsupported_reason = "expected boolean value";
    return false;
  }
  if (!node_adapter.TrySetBool(node_name, parsed, write_error)) {
    prepared.unsupported_reason =
        write_error.empty() ? "node rejected bool value" : std::move(write_error);
    return false;
  }
  prepared.backend_value = parsed ? "true" : "false";
  return true;
}

bool PrepareInt64Write(INodeMapAdapter& node_adapter, std::string_view node_name,
                       std::string_view requested_value, PreparedNodeWrite& prepared,
                       std::string& write_error) {
  prepared = PreparedNodeWrite{};
  std::int64_t parsed = 0;
  if (!ParseInt64(requested_value, parsed)) {
    prepared.unsupported_reason = "expected integer value";
    return false;
  }

  NodeNumericRange range;
  if (node_adapter.TryGetNumericRange(node_name, range)) {
    double value = static_cast<double>(parsed);
    std::string adjust_reason;
    if (ClampWithRange(value, range, adjust_reason)) {
      parsed = static_cast<std::int64_t>(std::llround(value));
      prepared.adjusted = true;
      prepared.adjustment_reason = std::move(adjust_reason);
    }
  }

  if (!node_adapter.TrySetInt64(node_name, parsed, write_error)) {
    prepared.unsupported_reason =
        write_error.empty() ? "node rejected integer value" : std::move(write_error);
    return false;
  }

  prepared.backend_value = std::to_string(parsed);
  return true;
}

bool PrepareFloat64Write(INodeMapAdapter& node_adapter, std::string_view node_name,
                         std::string_view requested_value, PreparedNodeWrite& prepared,
                         std::string& write_error) {
  prepared = PreparedNodeWrite{};
  double parsed = 0.0;
  if (!ParseDouble(requested_value, parsed)) {
    prepared.unsupported_reason = "expected floating-point value";
    return false;
  }

  NodeNumericRange range;
  if (node_adapter.TryGetNumericRange(node_name, range)) {
    std::string adjust_reason;
    if (ClampWithRange(parsed, range, adjust_reason)) {
      prepared.adjusted = true;
      prepared.adjustment_reason = std::move(adjust_reason);
    }
  }

  if (!node_adapter.TrySetFloat64(node_name, parsed, write_error)) {
    prepared.unsupported_reason =
        write_error.empty() ? "node rejected float value" : std::move(write_error);
    return false;
  }

  prepared.backend_value = FormatDouble(parsed);
  return true;
}

bool PrepareTextWrite(INodeMapAdapter& node_adapter, std::string_view node_name,
                      std::string_view generic_key, std::string_view requested_value,
                      NodeValueType node_type, const ParamRule& rule, PreparedNodeWrite& prepared,
                      std::string& write_error) {
  prepared = PreparedNodeWrite{};
  std::string transformed_value;
  bool transformed_adjusted = false;
  std::string transform_reason;

  if (node_type == NodeValueType::kEnumeration) {
    const std::vector<std::string> allowed = node_adapter.ListEnumValues(node_name);
    const ValueTransformHook transform_hook =
        rule.transform_hook == nullptr ? TransformEnumCaseInsensitive : rule.transform_hook;
    if (!transform_hook(std::string(requested_value), allowed, transformed_value,
                        transformed_adjusted, transform_reason)) {
      prepared.unsupported_reason = "value '" + std::string(requested_value) +
                                    "' is not supported for key '" + std::string(node_name) +
                                    "' (allowed: " + JoinEnumValues(allowed) + ")";
      return false;
    }
  } else {
    const ValueTransformHook transform_hook =
        rule.transform_hook == nullptr ? TransformIdentityValue : rule.transform_hook;
    if (!transform_hook(std::string(requested_value), {}, transformed_value, transformed_adjusted,
                        transform_reason)) {
      prepared.unsupported_reason =
          "value transform rejected input for key '" + std::string(generic_key) + "'";
      return false;
    }
  }

  if (!node_adapter.TrySetString(node_name, transformed_value, write_error)) {
    prepared.unsupported_reason =
        write_error.empty() ? "node rejected string value" : std::move(write_error);
    return false;
  }

  prepared.backend_value = transformed_value;
  prepared.adjusted = transformed_adjusted;
  prepared.adjustment_reason = std::move(transform_reason);
  return true;
}

std::unique_ptr<InMemoryNodeMapAdapter> BuildDefaultNodeAdapter() {
  auto adapter = std::make_unique<InMemoryNodeMapAdapter>();

  adapter->UpsertNode("ExposureTime", InMemoryNodeMapAdapter::NodeDefinition{
                                          .value_type = NodeValueType::kFloat64,
                                          .float64_value = 1200.0,
                                          .numeric_range =
                                              NodeNumericRange{
                                                  .min = std::optional<double>(5.0),
                                                  .max = std::optional<double>(10'000'000.0),
                                              },
                                      });
  adapter->UpsertNode("Gain", InMemoryNodeMapAdapter::NodeDefinition{
                                  .value_type = NodeValueType::kFloat64,
                                  .float64_value = 0.0,
                                  .numeric_range =
                                      NodeNumericRange{
                                          .min = std::optional<double>(0.0),
                                          .max = std::optional<double>(48.0),
                                      },
                              });
  adapter->UpsertNode("PixelFormat", InMemoryNodeMapAdapter::NodeDefinition{
                                         .value_type = NodeValueType::kEnumeration,
                                         .string_value = std::optional<std::string>("mono8"),
                                         .enum_values =
                                             std::vector<std::string>{
                                                 "mono8",
                                                 "mono12",
                                                 "rgb8",
                                             },
                                     });
  adapter->UpsertNode("RegionOfInterest", InMemoryNodeMapAdapter::NodeDefinition{
                                              .value_type = NodeValueType::kString,
                                              .string_value = std::optional<std::string>(""),
                                          });
  adapter->UpsertNode("Width", InMemoryNodeMapAdapter::NodeDefinition{
                                   .value_type = NodeValueType::kInt64,
                                   .int64_value = 1920,
                                   .numeric_range =
                                       NodeNumericRange{
                                           .min = std::optional<double>(64.0),
                                           .max = std::optional<double>(4096.0),
                                       },
                               });
  adapter->UpsertNode("Height", InMemoryNodeMapAdapter::NodeDefinition{
                                    .value_type = NodeValueType::kInt64,
                                    .int64_value = 1080,
                                    .numeric_range =
                                        NodeNumericRange{
                                            .min = std::optional<double>(64.0),
                                            .max = std::optional<double>(2160.0),
                                        },
                                });
  adapter->UpsertNode("OffsetX", InMemoryNodeMapAdapter::NodeDefinition{
                                     .value_type = NodeValueType::kInt64,
                                     .int64_value = 0,
                                     .numeric_range =
                                         NodeNumericRange{
                                             .min = std::optional<double>(0.0),
                                             .max = std::optional<double>(4095.0),
                                         },
                                 });
  adapter->UpsertNode("OffsetY", InMemoryNodeMapAdapter::NodeDefinition{
                                     .value_type = NodeValueType::kInt64,
                                     .int64_value = 0,
                                     .numeric_range =
                                         NodeNumericRange{
                                             .min = std::optional<double>(0.0),
                                             .max = std::optional<double>(2159.0),
                                         },
                                 });
  adapter->UpsertNode("GevSCPSPacketSize", InMemoryNodeMapAdapter::NodeDefinition{
                                               .value_type = NodeValueType::kInt64,
                                               .int64_value = 1500,
                                               .numeric_range =
                                                   NodeNumericRange{
                                                       .min = std::optional<double>(576.0),
                                                       .max = std::optional<double>(9000.0),
                                                   },
                                           });
  adapter->UpsertNode("GevSCPD", InMemoryNodeMapAdapter::NodeDefinition{
                                     .value_type = NodeValueType::kInt64,
                                     .int64_value = 0,
                                     .numeric_range =
                                         NodeNumericRange{
                                             .min = std::optional<double>(0.0),
                                             .max = std::optional<double>(100'000.0),
                                         },
                                 });
  adapter->UpsertNode("TriggerMode", InMemoryNodeMapAdapter::NodeDefinition{
                                         .value_type = NodeValueType::kEnumeration,
                                         .string_value = std::optional<std::string>("free_run"),
                                         .enum_values =
                                             std::vector<std::string>{
                                                 "free_run",
                                                 "software",
                                                 "hardware",
                                             },
                                     });
  adapter->UpsertNode("TriggerSource", InMemoryNodeMapAdapter::NodeDefinition{
                                           .value_type = NodeValueType::kEnumeration,
                                           .string_value = std::optional<std::string>("line0"),
                                           .enum_values =
                                               std::vector<std::string>{
                                                   "line0",
                                                   "line1",
                                                   "software",
                                               },
                                       });
  adapter->UpsertNode("TriggerActivation",
                      InMemoryNodeMapAdapter::NodeDefinition{
                          .value_type = NodeValueType::kEnumeration,
                          .string_value = std::optional<std::string>("rising_edge"),
                          .enum_values =
                              std::vector<std::string>{
                                  "rising_edge",
                                  "falling_edge",
                                  "any_edge",
                              },
                      });
  adapter->UpsertNode("AcquisitionFrameRate", InMemoryNodeMapAdapter::NodeDefinition{
                                                  .value_type = NodeValueType::kFloat64,
                                                  .float64_value = 30.0,
                                                  .numeric_range =
                                                      NodeNumericRange{
                                                          .min = std::optional<double>(1.0),
                                                          .max = std::optional<double>(240.0),
                                                      },
                                              });

  return adapter;
}

} // namespace

const char* ToString(ParamApplyMode mode) {
  switch (mode) {
  case ParamApplyMode::kStrict:
    return "strict";
  case ParamApplyMode::kBestEffort:
    return "best_effort";
  }
  return "strict";
}

bool ParseParamApplyMode(std::string_view raw_mode, ParamApplyMode& mode, std::string& error) {
  error.clear();
  const std::string normalized = ToLower(Trim(raw_mode));
  if (normalized.empty() || normalized == "strict") {
    mode = ParamApplyMode::kStrict;
    return true;
  }
  if (normalized == "best_effort" || normalized == "best-effort") {
    mode = ParamApplyMode::kBestEffort;
    return true;
  }

  error = "scenario apply_mode must be one of: strict, best_effort";
  return false;
}

std::unique_ptr<INodeMapAdapter> CreateDefaultNodeMapAdapter() {
  return BuildDefaultNodeAdapter();
}

bool ApplyParams(ICameraBackend& backend, const ParamKeyMap& key_map, INodeMapAdapter& node_adapter,
                 const std::vector<ApplyParamInput>& params, ParamApplyMode mode,
                 ApplyParamsResult& result, std::string& error) {
  result = ApplyParamsResult{};
  error.clear();

  const std::vector<ApplyParamInput> ordered_inputs = OrderApplyInputs(params);
  for (const ApplyParamInput& input : ordered_inputs) {
    const std::string generic_key = Trim(input.generic_key);
    if (generic_key.empty()) {
      continue;
    }
    const ParamRule& rule = ResolveParamRule(generic_key);
    const ParamApplyMode effective_mode = ResolveModeForRule(rule, mode);

    std::string node_name;
    if (!key_map.Resolve(generic_key, node_name)) {
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, std::nullopt,
                                      /*supported=*/false, "no generic->node mapping was found",
                                      effective_mode, result, error)) {
        return false;
      }
      continue;
    }

    const std::optional<std::string> resolved_node_name = node_name;
    if (!node_adapter.Has(node_name)) {
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                      /*supported=*/false,
                                      "mapped SDK node '" + node_name + "' is not available",
                                      effective_mode, result, error)) {
        return false;
      }
      continue;
    }

    ReadbackRow readback_row{
        .generic_key = generic_key,
        .node_name = node_name,
        .requested_value = input.requested_value,
    };

    AppliedParam applied{
        .generic_key = generic_key,
        .node_name = node_name,
        .requested_value = input.requested_value,
        .applied_value = input.requested_value,
    };

    PreparedNodeWrite prepared;
    std::string write_error;
    bool prepared_ok = false;
    const NodeValueType node_type = node_adapter.GetType(node_name);
    switch (node_type) {
    case NodeValueType::kBool:
      prepared_ok =
          PrepareBoolWrite(node_adapter, node_name, input.requested_value, prepared, write_error);
      break;
    case NodeValueType::kInt64:
      prepared_ok =
          PrepareInt64Write(node_adapter, node_name, input.requested_value, prepared, write_error);
      break;
    case NodeValueType::kFloat64:
      prepared_ok = PrepareFloat64Write(node_adapter, node_name, input.requested_value, prepared,
                                        write_error);
      break;
    case NodeValueType::kEnumeration:
    case NodeValueType::kString:
      prepared_ok = PrepareTextWrite(node_adapter, node_name, generic_key, input.requested_value,
                                     node_type, rule, prepared, write_error);
      break;
    case NodeValueType::kUnknown:
    default:
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                      /*supported=*/false, "node value type is unknown",
                                      effective_mode, result, error)) {
        return false;
      }
      continue;
    }

    if (!prepared_ok) {
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                      /*supported=*/true, prepared.unsupported_reason,
                                      effective_mode, result, error)) {
        return false;
      }
      continue;
    }
    applied.adjusted = prepared.adjusted;
    applied.adjustment_reason = std::move(prepared.adjustment_reason);

    std::string backend_error;
    if (!backend.SetParam(node_name, prepared.backend_value, backend_error)) {
      const std::string reason = "backend rejected mapped value: " +
                                 (backend_error.empty() ? "unknown error" : backend_error);
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                      /*supported=*/true, reason, effective_mode, result, error)) {
        return false;
      }
      continue;
    }

    readback_row.node_name = node_name;
    readback_row.supported = true;
    readback_row.applied = true;
    readback_row.adjusted = applied.adjusted;
    if (!applied.adjustment_reason.empty()) {
      readback_row.reason = applied.adjustment_reason;
    }
    std::string readback_error;
    if (!TryReadNodeValueAsString(node_adapter, node_name, readback_row.actual_value,
                                  readback_error)) {
      if (!readback_row.reason.empty()) {
        readback_row.reason += "; ";
      }
      readback_row.reason += "readback unavailable: " + readback_error;
    }
    if (rule.readback_hook != nullptr) {
      rule.readback_hook(applied, readback_row);
    }
    result.readback_rows.push_back(readback_row);

    applied.applied_value = prepared.backend_value;
    result.applied.push_back(std::move(applied));
  }

  return true;
}

} // namespace labops::backends::real_sdk
