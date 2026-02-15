#include "backends/real_sdk/node_map_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace labops::backends::real_sdk {

namespace {

bool IsSupportedStringValue(const std::vector<std::string>& values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename SetterFunc>
bool WithNodeForWrite(
    std::map<std::string, InMemoryNodeMapAdapter::NodeDefinition, std::less<>>& nodes,
    std::string_view key, std::string& error, SetterFunc&& setter) {
  error.clear();
  auto it = nodes.find(key);
  if (it == nodes.end()) {
    error = "unknown node key: " + std::string(key);
    return false;
  }
  return setter(it->second, error);
}

template <typename GetterFunc>
bool WithNodeForRead(
    const std::map<std::string, InMemoryNodeMapAdapter::NodeDefinition, std::less<>>& nodes,
    std::string_view key, GetterFunc&& getter) {
  const auto it = nodes.find(key);
  if (it == nodes.end()) {
    return false;
  }
  return getter(it->second);
}

bool ValidateNumericRange(std::string_view key, const NodeNumericRange& range, double value,
                          std::string& error) {
  if (range.min.has_value() && value < range.min.value()) {
    error = "value for key '" + std::string(key) + "' is below minimum " +
            std::to_string(range.min.value());
    return false;
  }
  if (range.max.has_value() && value > range.max.value()) {
    error = "value for key '" + std::string(key) + "' is above maximum " +
            std::to_string(range.max.value());
    return false;
  }
  return true;
}

} // namespace

void InMemoryNodeMapAdapter::UpsertNode(std::string key, NodeDefinition definition) {
  nodes_[std::move(key)] = std::move(definition);
}

bool InMemoryNodeMapAdapter::Has(std::string_view key) const {
  return nodes_.find(key) != nodes_.end();
}

NodeValueType InMemoryNodeMapAdapter::GetType(std::string_view key) const {
  const auto it = nodes_.find(key);
  if (it == nodes_.end()) {
    return NodeValueType::kUnknown;
  }
  return it->second.value_type;
}

bool InMemoryNodeMapAdapter::TryGetBool(std::string_view key, bool& value) const {
  return WithNodeForRead(nodes_, key, [&](const NodeDefinition& node) {
    if (node.value_type != NodeValueType::kBool || !node.bool_value.has_value()) {
      return false;
    }
    value = node.bool_value.value();
    return true;
  });
}

bool InMemoryNodeMapAdapter::TryGetInt64(std::string_view key, std::int64_t& value) const {
  return WithNodeForRead(nodes_, key, [&](const NodeDefinition& node) {
    if (node.value_type != NodeValueType::kInt64 || !node.int64_value.has_value()) {
      return false;
    }
    value = node.int64_value.value();
    return true;
  });
}

bool InMemoryNodeMapAdapter::TryGetFloat64(std::string_view key, double& value) const {
  return WithNodeForRead(nodes_, key, [&](const NodeDefinition& node) {
    if (node.value_type != NodeValueType::kFloat64 || !node.float64_value.has_value()) {
      return false;
    }
    value = node.float64_value.value();
    return true;
  });
}

bool InMemoryNodeMapAdapter::TryGetString(std::string_view key, std::string& value) const {
  return WithNodeForRead(nodes_, key, [&](const NodeDefinition& node) {
    if ((node.value_type != NodeValueType::kString &&
         node.value_type != NodeValueType::kEnumeration) ||
        !node.string_value.has_value()) {
      return false;
    }
    value = node.string_value.value();
    return true;
  });
}

bool InMemoryNodeMapAdapter::TrySetBool(std::string_view key, bool value, std::string& error) {
  return WithNodeForWrite(nodes_, key, error, [&](NodeDefinition& node, std::string& write_error) {
    if (node.value_type != NodeValueType::kBool) {
      write_error = "type mismatch for key '" + std::string(key) + "': expected bool";
      return false;
    }
    node.bool_value = value;
    return true;
  });
}

bool InMemoryNodeMapAdapter::TrySetInt64(std::string_view key, std::int64_t value,
                                         std::string& error) {
  return WithNodeForWrite(nodes_, key, error, [&](NodeDefinition& node, std::string& write_error) {
    if (node.value_type != NodeValueType::kInt64) {
      write_error = "type mismatch for key '" + std::string(key) + "': expected int64";
      return false;
    }
    if (!ValidateNumericRange(key, node.numeric_range, static_cast<double>(value), write_error)) {
      return false;
    }
    node.int64_value = value;
    return true;
  });
}

bool InMemoryNodeMapAdapter::TrySetFloat64(std::string_view key, double value, std::string& error) {
  return WithNodeForWrite(nodes_, key, error, [&](NodeDefinition& node, std::string& write_error) {
    if (node.value_type != NodeValueType::kFloat64) {
      write_error = "type mismatch for key '" + std::string(key) + "': expected float64";
      return false;
    }
    if (!std::isfinite(value)) {
      write_error = "value for key '" + std::string(key) + "' must be finite";
      return false;
    }
    if (!ValidateNumericRange(key, node.numeric_range, value, write_error)) {
      return false;
    }
    node.float64_value = value;
    return true;
  });
}

bool InMemoryNodeMapAdapter::TrySetString(std::string_view key, std::string_view value,
                                          std::string& error) {
  return WithNodeForWrite(nodes_, key, error, [&](NodeDefinition& node, std::string& write_error) {
    if (node.value_type == NodeValueType::kString) {
      node.string_value = std::string(value);
      return true;
    }
    if (node.value_type != NodeValueType::kEnumeration) {
      write_error = "type mismatch for key '" + std::string(key) + "': expected string/enum";
      return false;
    }
    if (!IsSupportedStringValue(node.enum_values, value)) {
      write_error =
          "value '" + std::string(value) + "' is not supported for key '" + std::string(key) + "'";
      return false;
    }
    node.string_value = std::string(value);
    return true;
  });
}

std::vector<std::string> InMemoryNodeMapAdapter::ListKeys() const {
  std::vector<std::string> keys;
  keys.reserve(nodes_.size());
  for (const auto& [key, _] : nodes_) {
    keys.push_back(key);
  }
  return keys;
}

std::vector<std::string> InMemoryNodeMapAdapter::ListEnumValues(std::string_view key) const {
  const auto it = nodes_.find(key);
  if (it == nodes_.end() || it->second.value_type != NodeValueType::kEnumeration) {
    return {};
  }
  return it->second.enum_values;
}

bool InMemoryNodeMapAdapter::TryGetNumericRange(std::string_view key,
                                                NodeNumericRange& range) const {
  return WithNodeForRead(nodes_, key, [&](const NodeDefinition& node) {
    if (node.value_type != NodeValueType::kInt64 && node.value_type != NodeValueType::kFloat64) {
      return false;
    }
    range = node.numeric_range;
    return true;
  });
}

} // namespace labops::backends::real_sdk
