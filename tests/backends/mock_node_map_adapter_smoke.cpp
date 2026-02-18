#include "backends/real_sdk/apply_params.hpp"
#include "backends/real_sdk/param_key_map.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using labops::backends::real_sdk::INodeMapAdapter;
using labops::backends::real_sdk::NodeNumericRange;
using labops::backends::real_sdk::NodeValueType;

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

class RecordingBackend final : public labops::backends::ICameraBackend {
public:
  bool Connect(std::string& /*error*/) override {
    return true;
  }

  bool Start(std::string& /*error*/) override {
    return true;
  }

  bool Stop(std::string& /*error*/) override {
    return true;
  }

  bool SetParam(const std::string& key, const std::string& value, std::string& error) override {
    if (key.empty() || value.empty()) {
      error = "backend key/value cannot be empty";
      return false;
    }
    set_calls_.emplace_back(key, value);
    params_[key] = value;
    return true;
  }

  labops::backends::BackendConfig DumpConfig() const override {
    return params_;
  }

  std::vector<labops::backends::FrameSample> PullFrames(std::chrono::milliseconds /*duration*/,
                                                        std::string& /*error*/) override {
    return {};
  }

  const std::vector<std::pair<std::string, std::string>>& set_calls() const {
    return set_calls_;
  }

private:
  labops::backends::BackendConfig params_;
  std::vector<std::pair<std::string, std::string>> set_calls_;
};

class MockNodeMapAdapter final : public INodeMapAdapter {
public:
  struct Node {
    NodeValueType value_type = NodeValueType::kUnknown;
    std::optional<bool> bool_value;
    std::optional<std::int64_t> int64_value;
    std::optional<double> float64_value;
    std::optional<std::string> string_value;
    std::vector<std::string> enum_values;
    NodeNumericRange numeric_range;
  };

  void UpsertNode(std::string key, Node node) {
    nodes_[std::move(key)] = std::move(node);
  }

  void RejectWritesForKey(std::string key) {
    rejected_keys_.insert(std::move(key));
  }

  bool Has(std::string_view key) const override {
    return nodes_.find(std::string(key)) != nodes_.end();
  }

  NodeValueType GetType(std::string_view key) const override {
    const auto it = nodes_.find(std::string(key));
    if (it == nodes_.end()) {
      return NodeValueType::kUnknown;
    }
    return it->second.value_type;
  }

  bool TryGetBool(std::string_view key, bool& value) const override {
    const Node* node = Find(key);
    if (node == nullptr || node->value_type != NodeValueType::kBool || !node->bool_value.has_value()) {
      return false;
    }
    value = node->bool_value.value();
    return true;
  }

  bool TryGetInt64(std::string_view key, std::int64_t& value) const override {
    const Node* node = Find(key);
    if (node == nullptr || node->value_type != NodeValueType::kInt64 ||
        !node->int64_value.has_value()) {
      return false;
    }
    value = node->int64_value.value();
    return true;
  }

  bool TryGetFloat64(std::string_view key, double& value) const override {
    const Node* node = Find(key);
    if (node == nullptr || node->value_type != NodeValueType::kFloat64 ||
        !node->float64_value.has_value()) {
      return false;
    }
    value = node->float64_value.value();
    return true;
  }

  bool TryGetString(std::string_view key, std::string& value) const override {
    const Node* node = Find(key);
    if (node == nullptr ||
        (node->value_type != NodeValueType::kString &&
         node->value_type != NodeValueType::kEnumeration) ||
        !node->string_value.has_value()) {
      return false;
    }
    value = node->string_value.value();
    return true;
  }

  bool TrySetBool(std::string_view key, bool value, std::string& error) override {
    Node* node = FindMutable(key);
    if (!PreflightWritable(node, key, NodeValueType::kBool, error)) {
      return false;
    }
    node->bool_value = value;
    set_key_order_.push_back(std::string(key));
    return true;
  }

  bool TrySetInt64(std::string_view key, std::int64_t value, std::string& error) override {
    Node* node = FindMutable(key);
    if (!PreflightWritable(node, key, NodeValueType::kInt64, error)) {
      return false;
    }
    if (node->numeric_range.min.has_value() && value < node->numeric_range.min.value()) {
      error = "mock range min violation";
      return false;
    }
    if (node->numeric_range.max.has_value() && value > node->numeric_range.max.value()) {
      error = "mock range max violation";
      return false;
    }
    node->int64_value = value;
    set_key_order_.push_back(std::string(key));
    return true;
  }

  bool TrySetFloat64(std::string_view key, double value, std::string& error) override {
    Node* node = FindMutable(key);
    if (!PreflightWritable(node, key, NodeValueType::kFloat64, error)) {
      return false;
    }
    if (node->numeric_range.min.has_value() && value < node->numeric_range.min.value()) {
      error = "mock range min violation";
      return false;
    }
    if (node->numeric_range.max.has_value() && value > node->numeric_range.max.value()) {
      error = "mock range max violation";
      return false;
    }
    node->float64_value = value;
    set_key_order_.push_back(std::string(key));
    return true;
  }

  bool TrySetString(std::string_view key, std::string_view value, std::string& error) override {
    Node* node = FindMutable(key);
    if (node == nullptr) {
      error = "mock node not found";
      return false;
    }
    if (rejected_keys_.find(std::string(key)) != rejected_keys_.end()) {
      error = "mock forced rejection";
      return false;
    }
    if (node->value_type != NodeValueType::kString &&
        node->value_type != NodeValueType::kEnumeration) {
      error = "mock type mismatch for string write";
      return false;
    }

    if (node->value_type == NodeValueType::kEnumeration && !node->enum_values.empty()) {
      const std::string requested_lower = ToLower(std::string(value));
      bool matched = false;
      for (const std::string& allowed : node->enum_values) {
        if (ToLower(allowed) == requested_lower) {
          matched = true;
          break;
        }
      }
      if (!matched) {
        error = "mock enum rejection";
        return false;
      }
    }

    node->string_value = std::string(value);
    set_key_order_.push_back(std::string(key));
    return true;
  }

  std::vector<std::string> ListKeys() const override {
    std::vector<std::string> keys;
    keys.reserve(nodes_.size());
    for (const auto& entry : nodes_) {
      keys.push_back(entry.first);
    }
    return keys;
  }

  std::vector<std::string> ListEnumValues(std::string_view key) const override {
    const Node* node = Find(key);
    if (node == nullptr || node->value_type != NodeValueType::kEnumeration) {
      return {};
    }
    return node->enum_values;
  }

  bool TryGetNumericRange(std::string_view key, NodeNumericRange& range) const override {
    const Node* node = Find(key);
    if (node == nullptr) {
      return false;
    }
    if (node->value_type != NodeValueType::kInt64 && node->value_type != NodeValueType::kFloat64) {
      return false;
    }
    range = node->numeric_range;
    return true;
  }

  const std::vector<std::string>& set_key_order() const {
    return set_key_order_;
  }

private:
  const Node* Find(std::string_view key) const {
    const auto it = nodes_.find(std::string(key));
    if (it == nodes_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  Node* FindMutable(std::string_view key) {
    const auto it = nodes_.find(std::string(key));
    if (it == nodes_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  bool PreflightWritable(Node* node, std::string_view key, NodeValueType expected_type,
                         std::string& error) const {
    if (node == nullptr) {
      error = "mock node not found";
      return false;
    }
    if (rejected_keys_.find(std::string(key)) != rejected_keys_.end()) {
      error = "mock forced rejection";
      return false;
    }
    if (node->value_type != expected_type) {
      error = "mock type mismatch";
      return false;
    }
    return true;
  }

  std::map<std::string, Node> nodes_;
  std::set<std::string> rejected_keys_;
  std::vector<std::string> set_key_order_;
};

labops::backends::real_sdk::ParamKeyMap BuildTestKeyMap() {
  using labops::backends::real_sdk::LoadParamKeyMapFromText;
  using labops::backends::real_sdk::ParamKeyMap;
  constexpr std::string_view kKeyMapText = R"JSON(
{
  "pixel_format": "PixelFormat",
  "exposure": "ExposureTime",
  "gain": "Gain",
  "roi_width": "Width",
  "roi_height": "Height",
  "roi_offset_x": "OffsetX",
  "roi_offset_y": "OffsetY"
}
)JSON";

  ParamKeyMap key_map;
  std::string error;
  if (!LoadParamKeyMapFromText(kKeyMapText, key_map, error)) {
    Fail("failed to build test key map: " + error);
  }
  return key_map;
}

void TestEnumMapping() {
  using labops::backends::real_sdk::ApplyParamInput;
  using labops::backends::real_sdk::ApplyParams;
  using labops::backends::real_sdk::ApplyParamsResult;
  using labops::backends::real_sdk::ParamApplyMode;

  RecordingBackend backend;
  MockNodeMapAdapter adapter;
  MockNodeMapAdapter::Node pixel_format;
  pixel_format.value_type = NodeValueType::kEnumeration;
  pixel_format.string_value = std::string("mono8");
  pixel_format.enum_values = {"mono8", "rgb8"};
  adapter.UpsertNode("PixelFormat", pixel_format);

  ApplyParamsResult result;
  std::string error;
  if (!ApplyParams(backend, BuildTestKeyMap(), adapter,
                   {
                       ApplyParamInput{"pixel_format", "RGB8"},
                   },
                   ParamApplyMode::kStrict, result, error)) {
    Fail("enum apply unexpectedly failed: " + error);
  }

  if (result.applied.size() != 1U || result.unsupported.size() != 0U ||
      result.readback_rows.size() != 1U) {
    Fail("enum mapping result counts are incorrect");
  }
  if (result.applied[0].node_name != "PixelFormat" || result.applied[0].applied_value != "rgb8" ||
      !result.applied[0].adjusted) {
    Fail("enum mapping should target PixelFormat and normalize value casing");
  }
  if (backend.set_calls().size() != 1U || backend.set_calls()[0].first != "PixelFormat" ||
      backend.set_calls()[0].second != "rgb8") {
    Fail("backend should receive one mapped PixelFormat set call");
  }
}

void TestRangeValidation() {
  using labops::backends::real_sdk::ApplyParamInput;
  using labops::backends::real_sdk::ApplyParams;
  using labops::backends::real_sdk::ApplyParamsResult;
  using labops::backends::real_sdk::ParamApplyMode;

  RecordingBackend backend;
  MockNodeMapAdapter adapter;
  MockNodeMapAdapter::Node exposure;
  exposure.value_type = NodeValueType::kFloat64;
  exposure.float64_value = 100.0;
  exposure.numeric_range.min = 5.0;
  exposure.numeric_range.max = 1000.0;
  adapter.UpsertNode("ExposureTime", exposure);

  ApplyParamsResult result;
  std::string error;
  if (!ApplyParams(backend, BuildTestKeyMap(), adapter,
                   {
                       ApplyParamInput{"exposure", "2500"},
                   },
                   ParamApplyMode::kStrict, result, error)) {
    Fail("range-validation apply unexpectedly failed: " + error);
  }

  if (result.applied.size() != 1U || !result.applied[0].adjusted ||
      result.applied[0].applied_value != "1000") {
    Fail("exposure should be clamped to the mock range max (1000)");
  }
  if (result.readback_rows.size() != 1U || result.readback_rows[0].actual_value != "1000") {
    Fail("range-validation readback should capture clamped actual value");
  }
  if (backend.set_calls().size() != 1U || backend.set_calls()[0].first != "ExposureTime" ||
      backend.set_calls()[0].second != "1000") {
    Fail("backend should receive clamped ExposureTime value");
  }
}

void TestStrictVsBestEffort() {
  using labops::backends::real_sdk::ApplyParamInput;
  using labops::backends::real_sdk::ApplyParams;
  using labops::backends::real_sdk::ApplyParamsResult;
  using labops::backends::real_sdk::ParamApplyMode;

  auto build_adapter = []() {
    MockNodeMapAdapter adapter;
    MockNodeMapAdapter::Node gain;
    gain.value_type = NodeValueType::kFloat64;
    gain.float64_value = 0.0;
    gain.numeric_range.min = 0.0;
    gain.numeric_range.max = 24.0;
    adapter.UpsertNode("Gain", gain);
    return adapter;
  };

  {
    RecordingBackend backend;
    MockNodeMapAdapter adapter = build_adapter();
    ApplyParamsResult result;
    std::string error;
    if (ApplyParams(backend, BuildTestKeyMap(), adapter,
                    {
                        ApplyParamInput{"gain", "10"},
                        ApplyParamInput{"unknown_knob", "1"},
                    },
                    ParamApplyMode::kStrict, result, error)) {
      Fail("strict mode should fail when unsupported input is present");
    }
    AssertContains(error, "unsupported parameter 'unknown_knob'");
    if (result.applied.size() != 1U || result.unsupported.size() != 1U) {
      Fail("strict mode should record one applied and one unsupported entry");
    }
  }

  {
    RecordingBackend backend;
    MockNodeMapAdapter adapter = build_adapter();
    ApplyParamsResult result;
    std::string error;
    if (!ApplyParams(backend, BuildTestKeyMap(), adapter,
                     {
                         ApplyParamInput{"gain", "10"},
                         ApplyParamInput{"unknown_knob", "1"},
                     },
                     ParamApplyMode::kBestEffort, result, error)) {
      Fail("best-effort mode should continue on unsupported input: " + error);
    }
    if (result.applied.size() != 1U || result.unsupported.size() != 1U ||
        backend.set_calls().size() != 1U) {
      Fail("best-effort mode should keep successful writes while recording unsupported input");
    }
  }
}

void TestRoiOrdering() {
  using labops::backends::real_sdk::ApplyParamInput;
  using labops::backends::real_sdk::ApplyParams;
  using labops::backends::real_sdk::ApplyParamsResult;
  using labops::backends::real_sdk::ParamApplyMode;

  RecordingBackend backend;
  MockNodeMapAdapter adapter;

  MockNodeMapAdapter::Node width;
  width.value_type = NodeValueType::kInt64;
  width.int64_value = 1920;
  width.numeric_range.min = 64.0;
  width.numeric_range.max = 4096.0;
  adapter.UpsertNode("Width", width);

  MockNodeMapAdapter::Node height;
  height.value_type = NodeValueType::kInt64;
  height.int64_value = 1080;
  height.numeric_range.min = 64.0;
  height.numeric_range.max = 2160.0;
  adapter.UpsertNode("Height", height);

  MockNodeMapAdapter::Node offset_x;
  offset_x.value_type = NodeValueType::kInt64;
  offset_x.int64_value = 0;
  offset_x.numeric_range.min = 0.0;
  offset_x.numeric_range.max = 4095.0;
  adapter.UpsertNode("OffsetX", offset_x);

  MockNodeMapAdapter::Node offset_y;
  offset_y.value_type = NodeValueType::kInt64;
  offset_y.int64_value = 0;
  offset_y.numeric_range.min = 0.0;
  offset_y.numeric_range.max = 2159.0;
  adapter.UpsertNode("OffsetY", offset_y);

  ApplyParamsResult result;
  std::string error;
  if (!ApplyParams(backend, BuildTestKeyMap(), adapter,
                   {
                       ApplyParamInput{"roi_offset_x", "400"},
                       ApplyParamInput{"roi_offset_y", "200"},
                       ApplyParamInput{"roi_width", "3000"},
                       ApplyParamInput{"roi_height", "1600"},
                   },
                   ParamApplyMode::kBestEffort, result, error)) {
    Fail("ROI ordering apply unexpectedly failed: " + error);
  }

  const auto& calls = backend.set_calls();
  if (calls.size() != 4U) {
    Fail("ROI apply should produce exactly four backend set calls");
  }
  if (calls[0].first != "Width" || calls[1].first != "Height" || calls[2].first != "OffsetX" ||
      calls[3].first != "OffsetY") {
    Fail("ROI ordering should apply width/height before offsets");
  }
}

} // namespace

int main() {
  TestEnumMapping();
  TestRangeValidation();
  TestStrictVsBestEffort();
  TestRoiOrdering();
  return 0;
}
