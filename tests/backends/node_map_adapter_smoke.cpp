#include "backends/real_sdk/node_map_adapter.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

} // namespace

int main() {
  using labops::backends::real_sdk::InMemoryNodeMapAdapter;
  using labops::backends::real_sdk::NodeNumericRange;
  using labops::backends::real_sdk::NodeValueType;

  InMemoryNodeMapAdapter adapter;
  adapter.UpsertNode(
      "camera.fps",
      InMemoryNodeMapAdapter::NodeDefinition{
          .value_type = NodeValueType::kInt64,
          .int64_value = 30,
          .numeric_range = NodeNumericRange{
              .min = std::optional<double>(1.0),
              .max = std::optional<double>(240.0),
          },
      });
  adapter.UpsertNode(
      "camera.exposure_us",
      InMemoryNodeMapAdapter::NodeDefinition{
          .value_type = NodeValueType::kFloat64,
          .float64_value = 1200.0,
          .numeric_range = NodeNumericRange{
              .min = std::optional<double>(50.0),
              .max = std::optional<double>(1'000'000.0),
          },
      });
  adapter.UpsertNode(
      "camera.pixel_format",
      InMemoryNodeMapAdapter::NodeDefinition{
          .value_type = NodeValueType::kEnumeration,
          .string_value = std::optional<std::string>("mono8"),
          .enum_values = std::vector<std::string>{"mono8", "mono12", "rgb8"},
      });

  // Core done-condition signal: callers can query key existence before any
  // write/apply operation is attempted.
  if (!adapter.Has("camera.fps")) {
    Fail("expected camera.fps to exist before apply");
  }
  if (adapter.Has("camera.not_real")) {
    Fail("unexpected key was reported as supported");
  }

  if (adapter.GetType("camera.fps") != NodeValueType::kInt64) {
    Fail("camera.fps type mismatch");
  }
  if (adapter.GetType("camera.pixel_format") != NodeValueType::kEnumeration) {
    Fail("camera.pixel_format type mismatch");
  }
  if (adapter.GetType("camera.missing") != NodeValueType::kUnknown) {
    Fail("missing key must return kUnknown");
  }

  std::int64_t fps = 0;
  if (!adapter.TryGetInt64("camera.fps", fps) || fps != 30) {
    Fail("failed to read initial fps value");
  }
  std::string error;
  if (!adapter.TrySetInt64("camera.fps", 120, error)) {
    Fail("failed to set valid fps value");
  }
  if (!adapter.TryGetInt64("camera.fps", fps) || fps != 120) {
    Fail("failed to read updated fps value");
  }

  if (adapter.TrySetInt64("camera.fps", 500, error)) {
    Fail("expected out-of-range fps write to fail");
  }
  AssertContains(error, "above maximum");

  double exposure = 0.0;
  if (!adapter.TryGetFloat64("camera.exposure_us", exposure) || exposure != 1200.0) {
    Fail("failed to read exposure");
  }

  const std::vector<std::string> enum_values = adapter.ListEnumValues("camera.pixel_format");
  if (enum_values.size() != 3U || enum_values[0] != "mono8" || enum_values[2] != "rgb8") {
    Fail("enum listing mismatch");
  }
  if (!adapter.TrySetString("camera.pixel_format", "rgb8", error)) {
    Fail("failed to set supported enum value");
  }
  std::string pixel_format;
  if (!adapter.TryGetString("camera.pixel_format", pixel_format) || pixel_format != "rgb8") {
    Fail("failed to read updated enum value");
  }
  if (adapter.TrySetString("camera.pixel_format", "yuv422", error)) {
    Fail("expected unsupported enum value to fail");
  }
  AssertContains(error, "not supported");

  NodeNumericRange fps_range;
  if (!adapter.TryGetNumericRange("camera.fps", fps_range)) {
    Fail("expected numeric range for camera.fps");
  }
  if (!fps_range.min.has_value() || !fps_range.max.has_value() || fps_range.min.value() != 1.0 ||
      fps_range.max.value() != 240.0) {
    Fail("unexpected fps range values");
  }
  NodeNumericRange enum_range;
  if (adapter.TryGetNumericRange("camera.pixel_format", enum_range)) {
    Fail("enum node should not report numeric range");
  }

  const std::vector<std::string> keys = adapter.ListKeys();
  if (keys.size() != 3U || keys[0] != "camera.exposure_us" || keys[1] != "camera.fps" ||
      keys[2] != "camera.pixel_format") {
    Fail("key listing mismatch");
  }

  std::cout << "node_map_adapter_smoke: ok\n";
  return 0;
}
