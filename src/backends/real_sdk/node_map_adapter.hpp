#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::real_sdk {

// Generic value shape for parameter keys that will eventually map to vendor SDK
// node names. Keeping this explicit avoids stringly-typed conversions scattered
// through run orchestration.
enum class NodeValueType {
  kUnknown = 0,
  kBool,
  kInt64,
  kFloat64,
  kString,
  kEnumeration,
};

// Best-effort range metadata for numeric nodes.
// Some SDK nodes expose both bounds, some expose one side, and some expose
// neither. Optional fields let callers handle all three cases explicitly.
struct NodeNumericRange {
  std::optional<double> min;
  std::optional<double> max;
};

// Parameter-bridge abstraction (generic key -> SDK node intent).
//
// This interface is intentionally read/query-heavy first so code can validate
// settings (`has`, type, enum/range support) before any camera-side apply path
// is wired.
class INodeMapAdapter {
public:
  virtual ~INodeMapAdapter() = default;

  virtual bool Has(std::string_view key) const = 0;
  virtual NodeValueType GetType(std::string_view key) const = 0;

  virtual bool TryGetBool(std::string_view key, bool& value) const = 0;
  virtual bool TryGetInt64(std::string_view key, std::int64_t& value) const = 0;
  virtual bool TryGetFloat64(std::string_view key, double& value) const = 0;
  virtual bool TryGetString(std::string_view key, std::string& value) const = 0;

  virtual bool TrySetBool(std::string_view key, bool value, std::string& error) = 0;
  virtual bool TrySetInt64(std::string_view key, std::int64_t value, std::string& error) = 0;
  virtual bool TrySetFloat64(std::string_view key, double value, std::string& error) = 0;
  virtual bool TrySetString(std::string_view key, std::string_view value, std::string& error) = 0;

  virtual std::vector<std::string> ListKeys() const = 0;
  virtual std::vector<std::string> ListEnumValues(std::string_view key) const = 0;
  virtual bool TryGetNumericRange(std::string_view key, NodeNumericRange& range) const = 0;
};

// In-memory adapter used for deterministic OSS tests and early real-backend
// bring-up before proprietary SDK node calls are linked.
class InMemoryNodeMapAdapter final : public INodeMapAdapter {
public:
  struct NodeDefinition {
    NodeValueType value_type = NodeValueType::kUnknown;
    std::optional<bool> bool_value;
    std::optional<std::int64_t> int64_value;
    std::optional<double> float64_value;
    std::optional<std::string> string_value;
    std::vector<std::string> enum_values;
    NodeNumericRange numeric_range;
  };

  void UpsertNode(std::string key, NodeDefinition definition);

  bool Has(std::string_view key) const override;
  NodeValueType GetType(std::string_view key) const override;

  bool TryGetBool(std::string_view key, bool& value) const override;
  bool TryGetInt64(std::string_view key, std::int64_t& value) const override;
  bool TryGetFloat64(std::string_view key, double& value) const override;
  bool TryGetString(std::string_view key, std::string& value) const override;

  bool TrySetBool(std::string_view key, bool value, std::string& error) override;
  bool TrySetInt64(std::string_view key, std::int64_t value, std::string& error) override;
  bool TrySetFloat64(std::string_view key, double value, std::string& error) override;
  bool TrySetString(std::string_view key, std::string_view value, std::string& error) override;

  std::vector<std::string> ListKeys() const override;
  std::vector<std::string> ListEnumValues(std::string_view key) const override;
  bool TryGetNumericRange(std::string_view key, NodeNumericRange& range) const override;

private:
  std::map<std::string, NodeDefinition, std::less<>> nodes_;
};

} // namespace labops::backends::real_sdk
