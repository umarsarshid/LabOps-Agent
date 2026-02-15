#include "backends/real_sdk/param_key_map.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(const std::vector<std::string>& values, std::string_view needle) {
  for (const std::string& value : values) {
    if (value == needle) {
      return;
    }
  }
  std::cerr << "expected list to contain: " << needle << '\n';
  std::abort();
}

} // namespace

int main() {
  using labops::backends::real_sdk::LoadParamKeyMapFromFile;
  using labops::backends::real_sdk::ParamKeyMap;
  using labops::backends::real_sdk::ResolveDefaultParamKeyMapPath;

  const fs::path default_map = ResolveDefaultParamKeyMapPath();
  ParamKeyMap map;
  std::string error;
  if (!LoadParamKeyMapFromFile(default_map, map, error)) {
    Fail("failed to load default param key map: " + error);
  }

  // Milestone contract: mapping must answer support checks before apply path.
  if (!map.Has("exposure") || !map.Has("gain") || !map.Has("pixel_format") || !map.Has("roi") ||
      !map.Has("trigger_mode") || !map.Has("trigger_source") || !map.Has("frame_rate")) {
    Fail("default map missing one or more required first keys");
  }
  if (map.Has("unknown_key")) {
    Fail("unknown key should not appear in map");
  }

  std::string node_name;
  if (!map.Resolve("exposure", node_name) || node_name.empty()) {
    Fail("failed to resolve exposure mapping");
  }
  if (!map.Resolve("frame_rate", node_name) || node_name.empty()) {
    Fail("failed to resolve frame_rate mapping");
  }

  const std::vector<std::string> keys = map.ListGenericKeys();
  AssertContains(keys, "exposure");
  AssertContains(keys, "gain");
  AssertContains(keys, "pixel_format");
  AssertContains(keys, "roi");
  AssertContains(keys, "trigger_mode");
  AssertContains(keys, "trigger_source");
  AssertContains(keys, "frame_rate");

  // Data-driven update proof: changing JSON content updates behavior without
  // touching core C++ logic.
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-param-key-map-smoke-" + std::to_string(now_ms));
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root for map override test");
  }

  const fs::path override_path = temp_root / "param_key_map_override.json";
  {
    std::ofstream out(override_path, std::ios::binary);
    if (!out) {
      Fail("failed to create override param key map file");
    }
    out << "{\n"
        << "  \"exposure\": \"ExposureTimeAbs\",\n"
        << "  \"gain\": \"GainRaw\",\n"
        << "  \"pixel_format\": \"PixelFormat\",\n"
        << "  \"roi\": \"RoiSelector\",\n"
        << "  \"trigger_mode\": \"TriggerMode\",\n"
        << "  \"trigger_source\": \"TriggerSource\",\n"
        << "  \"frame_rate\": \"AcquisitionFrameRateAbs\"\n"
        << "}\n";
  }

  ParamKeyMap override_map;
  if (!LoadParamKeyMapFromFile(override_path, override_map, error)) {
    Fail("failed to load override map: " + error);
  }

  if (!override_map.Resolve("exposure", node_name) || node_name != "ExposureTimeAbs") {
    Fail("override map did not apply updated exposure node");
  }
  if (!override_map.Resolve("frame_rate", node_name) || node_name != "AcquisitionFrameRateAbs") {
    Fail("override map did not apply updated frame_rate node");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "param_key_map_smoke: ok\n";
  return 0;
}
