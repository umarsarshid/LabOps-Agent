#include "backends/real_sdk/apply_params.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>

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

  for (const ApplyParamInput& input : params) {
    const std::string generic_key = Trim(input.generic_key);
    if (generic_key.empty()) {
      continue;
    }

    std::string node_name;
    if (!key_map.Resolve(generic_key, node_name)) {
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, std::nullopt,
                                      /*supported=*/false, "no generic->node mapping was found",
                                      mode, result, error)) {
        return false;
      }
      continue;
    }

    const std::optional<std::string> resolved_node_name = node_name;
    if (!node_adapter.Has(node_name)) {
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                      /*supported=*/false,
                                      "mapped SDK node '" + node_name + "' is not available", mode,
                                      result, error)) {
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

    std::string write_error;
    std::string backend_value = input.requested_value;
    const NodeValueType node_type = node_adapter.GetType(node_name);
    switch (node_type) {
    case NodeValueType::kBool: {
      bool parsed = false;
      if (!ParseBool(input.requested_value, parsed)) {
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, "expected boolean value", mode, result,
                                        error)) {
          return false;
        }
        continue;
      }
      if (!node_adapter.TrySetBool(node_name, parsed, write_error)) {
        const std::string reason = write_error.empty() ? "node rejected bool value" : write_error;
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, reason, mode, result, error)) {
          return false;
        }
        continue;
      }
      backend_value = parsed ? "true" : "false";
      break;
    }
    case NodeValueType::kInt64: {
      std::int64_t parsed = 0;
      if (!ParseInt64(input.requested_value, parsed)) {
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, "expected integer value", mode, result,
                                        error)) {
          return false;
        }
        continue;
      }

      NodeNumericRange range;
      if (node_adapter.TryGetNumericRange(node_name, range)) {
        double value = static_cast<double>(parsed);
        std::string adjust_reason;
        if (ClampWithRange(value, range, adjust_reason)) {
          parsed = static_cast<std::int64_t>(std::llround(value));
          applied.adjusted = true;
          applied.adjustment_reason = std::move(adjust_reason);
        }
      }

      if (!node_adapter.TrySetInt64(node_name, parsed, write_error)) {
        const std::string reason =
            write_error.empty() ? "node rejected integer value" : write_error;
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, reason, mode, result, error)) {
          return false;
        }
        continue;
      }

      backend_value = std::to_string(parsed);
      break;
    }
    case NodeValueType::kFloat64: {
      double parsed = 0.0;
      if (!ParseDouble(input.requested_value, parsed)) {
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, "expected floating-point value", mode,
                                        result, error)) {
          return false;
        }
        continue;
      }

      NodeNumericRange range;
      if (node_adapter.TryGetNumericRange(node_name, range)) {
        std::string adjust_reason;
        if (ClampWithRange(parsed, range, adjust_reason)) {
          applied.adjusted = true;
          applied.adjustment_reason = std::move(adjust_reason);
        }
      }

      if (!node_adapter.TrySetFloat64(node_name, parsed, write_error)) {
        const std::string reason = write_error.empty() ? "node rejected float value" : write_error;
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, reason, mode, result, error)) {
          return false;
        }
        continue;
      }

      backend_value = FormatDouble(parsed);
      break;
    }
    case NodeValueType::kEnumeration:
    case NodeValueType::kString: {
      std::string normalized_value = input.requested_value;
      if (node_type == NodeValueType::kEnumeration) {
        const std::vector<std::string> allowed = node_adapter.ListEnumValues(node_name);
        const std::optional<std::string> canonical =
            FindCaseInsensitiveEnumValue(allowed, input.requested_value);
        if (canonical.has_value() && canonical.value() != input.requested_value) {
          normalized_value = canonical.value();
          applied.adjusted = true;
          applied.adjustment_reason = "normalized enumeration value casing";
        }
      }

      if (!node_adapter.TrySetString(node_name, normalized_value, write_error)) {
        const std::string reason = write_error.empty() ? "node rejected string value" : write_error;
        if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                        /*supported=*/true, reason, mode, result, error)) {
          return false;
        }
        continue;
      }

      backend_value = normalized_value;
      break;
    }
    case NodeValueType::kUnknown:
    default:
      if (!RecordUnsupportedParameter(generic_key, input.requested_value, resolved_node_name,
                                      /*supported=*/false, "node value type is unknown", mode,
                                      result, error)) {
        return false;
      }
      continue;
    }

    std::string backend_error;
    if (!backend.SetParam(node_name, backend_value, backend_error)) {
      readback_row.node_name = node_name;
      readback_row.supported = true;
      readback_row.applied = false;
      readback_row.adjusted = applied.adjusted;
      readback_row.reason = "backend rejected mapped value: " +
                            (backend_error.empty() ? "unknown error" : backend_error);
      result.readback_rows.push_back(readback_row);
      error = "failed to set mapped backend parameter '" + node_name + "' for generic key '" +
              generic_key +
              "': " + (backend_error.empty() ? "backend rejected value" : backend_error);
      return false;
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
    result.readback_rows.push_back(readback_row);

    applied.applied_value = backend_value;
    result.applied.push_back(std::move(applied));
  }

  return true;
}

} // namespace labops::backends::real_sdk
