#include "../common/assertions.hpp"
#include "backends/real_sdk/apply_params.hpp"
#include "backends/real_sdk/param_key_map.hpp"

#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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
      error = "empty key/value is not allowed";
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

struct KeyCase {
  std::string_view generic_key;
  std::string_view requested_value;
  std::string_view expected_node_name;
  std::string_view expected_applied_value;
  bool expected_adjusted = false;
};

void AssertSupportedKeyCoverage(const labops::backends::real_sdk::ParamKeyMap& key_map,
                                const std::vector<KeyCase>& key_cases) {
  std::set<std::string> expected_supported_keys;
  for (const std::string& key : key_map.ListGenericKeys()) {
    expected_supported_keys.insert(key);
  }

  std::set<std::string> covered_keys;
  for (const KeyCase& key_case : key_cases) {
    covered_keys.insert(std::string(key_case.generic_key));
  }

  if (covered_keys != expected_supported_keys) {
    labops::tests::common::Fail("table-driven key cases must cover every supported key");
  }
}

void RunSingleCase(const labops::backends::real_sdk::ParamKeyMap& key_map,
                   const KeyCase& key_case) {
  using labops::backends::real_sdk::ApplyParamInput;
  using labops::backends::real_sdk::ApplyParams;
  using labops::backends::real_sdk::ApplyParamsResult;
  using labops::backends::real_sdk::CreateDefaultNodeMapAdapter;
  using labops::backends::real_sdk::ParamApplyMode;

  RecordingBackend backend;
  std::unique_ptr<labops::backends::real_sdk::INodeMapAdapter> adapter =
      CreateDefaultNodeMapAdapter();
  ApplyParamsResult result;
  std::string error;
  if (!ApplyParams(backend, key_map, *adapter,
                   {
                       ApplyParamInput{std::string(key_case.generic_key),
                                       std::string(key_case.requested_value)},
                   },
                   ParamApplyMode::kStrict, result, error)) {
    labops::tests::common::Fail("table-driven apply unexpectedly failed for key '" +
                                std::string(key_case.generic_key) + "': " + error);
  }

  if (result.applied.size() != 1U || result.unsupported.size() != 0U ||
      result.readback_rows.size() != 1U) {
    labops::tests::common::Fail("table-driven apply produced unexpected result counts for key '" +
                                std::string(key_case.generic_key) + "'");
  }

  const auto& applied = result.applied.front();
  if (applied.generic_key != key_case.generic_key ||
      applied.node_name != key_case.expected_node_name ||
      applied.applied_value != key_case.expected_applied_value ||
      applied.adjusted != key_case.expected_adjusted) {
    labops::tests::common::Fail("table-driven apply entry mismatch for key '" +
                                std::string(key_case.generic_key) + "'");
  }

  const auto& readback = result.readback_rows.front();
  if (readback.generic_key != key_case.generic_key ||
      readback.node_name != key_case.expected_node_name || !readback.supported ||
      !readback.applied || readback.actual_value != key_case.expected_applied_value ||
      readback.adjusted != key_case.expected_adjusted) {
    labops::tests::common::Fail("table-driven readback mismatch for key '" +
                                std::string(key_case.generic_key) + "'");
  }

  if (backend.set_calls().size() != 1U ||
      backend.set_calls().front().first != key_case.expected_node_name ||
      backend.set_calls().front().second != key_case.expected_applied_value) {
    labops::tests::common::Fail("backend mapped write mismatch for key '" +
                                std::string(key_case.generic_key) + "'");
  }
}

} // namespace

int main() {
  using labops::backends::real_sdk::LoadParamKeyMapFromFile;
  using labops::backends::real_sdk::ParamKeyMap;
  using labops::backends::real_sdk::ResolveDefaultParamKeyMapPath;

  ParamKeyMap key_map;
  std::string error;
  if (!LoadParamKeyMapFromFile(ResolveDefaultParamKeyMapPath(), key_map, error)) {
    labops::tests::common::Fail("failed to load default param key map: " + error);
  }

  // Table-driven contract checks: one success-path case per supported key.
  const std::vector<KeyCase> key_cases = {
      {.generic_key = "exposure",
       .requested_value = "2400",
       .expected_node_name = "ExposureTime",
       .expected_applied_value = "2400"},
      {.generic_key = "gain",
       .requested_value = "3.5",
       .expected_node_name = "Gain",
       .expected_applied_value = "3.5"},
      {.generic_key = "pixel_format",
       .requested_value = "RGB8",
       .expected_node_name = "PixelFormat",
       .expected_applied_value = "rgb8",
       .expected_adjusted = true},
      {.generic_key = "roi_width",
       .requested_value = "640",
       .expected_node_name = "Width",
       .expected_applied_value = "640"},
      {.generic_key = "roi_height",
       .requested_value = "480",
       .expected_node_name = "Height",
       .expected_applied_value = "480"},
      {.generic_key = "roi_offset_x",
       .requested_value = "10",
       .expected_node_name = "OffsetX",
       .expected_applied_value = "10"},
      {.generic_key = "roi_offset_y",
       .requested_value = "20",
       .expected_node_name = "OffsetY",
       .expected_applied_value = "20"},
      {.generic_key = "roi",
       .requested_value = "x=0,y=0,width=640,height=480",
       .expected_node_name = "RegionOfInterest",
       .expected_applied_value = "x=0,y=0,width=640,height=480"},
      {.generic_key = "packet_size_bytes",
       .requested_value = "1500",
       .expected_node_name = "GevSCPSPacketSize",
       .expected_applied_value = "1500"},
      {.generic_key = "inter_packet_delay_us",
       .requested_value = "250",
       .expected_node_name = "GevSCPD",
       .expected_applied_value = "250"},
      {.generic_key = "trigger_mode",
       .requested_value = "HARDWARE",
       .expected_node_name = "TriggerMode",
       .expected_applied_value = "hardware",
       .expected_adjusted = true},
      {.generic_key = "trigger_source",
       .requested_value = "LINE1",
       .expected_node_name = "TriggerSource",
       .expected_applied_value = "line1",
       .expected_adjusted = true},
      {.generic_key = "trigger_activation",
       .requested_value = "FALLING_EDGE",
       .expected_node_name = "TriggerActivation",
       .expected_applied_value = "falling_edge",
       .expected_adjusted = true},
      {.generic_key = "frame_rate",
       .requested_value = "120",
       .expected_node_name = "AcquisitionFrameRate",
       .expected_applied_value = "120"},
  };

  AssertSupportedKeyCoverage(key_map, key_cases);
  for (const KeyCase& key_case : key_cases) {
    RunSingleCase(key_map, key_case);
  }

  return 0;
}
