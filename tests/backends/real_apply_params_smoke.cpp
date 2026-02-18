#include "backends/real_sdk/apply_params.hpp"
#include "backends/real_sdk/param_key_map.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

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

private:
  labops::backends::BackendConfig params_;
};

} // namespace

int main() {
  using labops::backends::real_sdk::ApplyParamInput;
  using labops::backends::real_sdk::ApplyParams;
  using labops::backends::real_sdk::ApplyParamsResult;
  using labops::backends::real_sdk::CreateDefaultNodeMapAdapter;
  using labops::backends::real_sdk::LoadParamKeyMapFromFile;
  using labops::backends::real_sdk::ParamApplyMode;
  using labops::backends::real_sdk::ParamKeyMap;
  using labops::backends::real_sdk::ResolveDefaultParamKeyMapPath;

  ParamKeyMap key_map;
  std::string error;
  if (!LoadParamKeyMapFromFile(ResolveDefaultParamKeyMapPath(), key_map, error)) {
    Fail("failed to load default param key map: " + error);
  }

  // Strict mode should fail immediately when a parameter is unsupported.
  {
    RecordingBackend backend;
    std::unique_ptr<labops::backends::real_sdk::INodeMapAdapter> adapter =
        CreateDefaultNodeMapAdapter();
    ApplyParamsResult result;
    if (ApplyParams(backend, key_map, *adapter,
                    {
                        ApplyParamInput{"frame_rate", "60"},
                        ApplyParamInput{"unknown_knob", "1"},
                    },
                    ParamApplyMode::kStrict, result, error)) {
      Fail("strict apply should fail when unsupported parameter is present");
    }

    AssertContains(error, "unsupported parameter 'unknown_knob'");
    if (result.applied.size() != 1U) {
      Fail("strict apply should record already-applied parameters before failing");
    }
    if (result.unsupported.size() != 1U) {
      Fail("strict apply should report one unsupported parameter");
    }
    if (result.readback_rows.size() != 2U) {
      Fail("strict apply should capture readback rows for all attempted settings");
    }
    if (result.readback_rows[0].generic_key != "frame_rate" || !result.readback_rows[0].supported ||
        !result.readback_rows[0].applied || result.readback_rows[0].actual_value != "60") {
      Fail("strict apply readback for applied frame_rate is incorrect");
    }
    if (result.readback_rows[1].generic_key != "unknown_knob" ||
        result.readback_rows[1].supported || result.readback_rows[1].applied) {
      Fail("strict apply readback for unsupported key is incorrect");
    }
  }

  // Best-effort mode should continue applying supported parameters, emit
  // unsupported records, and keep adjusted values explicit for event wiring.
  {
    RecordingBackend backend;
    std::unique_ptr<labops::backends::real_sdk::INodeMapAdapter> adapter =
        CreateDefaultNodeMapAdapter();
    ApplyParamsResult result;
    if (!ApplyParams(backend, key_map, *adapter,
                     {
                         ApplyParamInput{"frame_rate", "1000"},
                         ApplyParamInput{"exposure", "20000000"},
                         ApplyParamInput{"gain", "-2"},
                         ApplyParamInput{"pixel_format", "mono8"},
                         ApplyParamInput{"unknown_knob", "1"},
                     },
                     ParamApplyMode::kBestEffort, result, error)) {
      Fail("best-effort apply unexpectedly failed: " + error);
    }

    if (result.applied.size() != 4U) {
      Fail("best-effort apply should keep 4 supported parameters");
    }
    if (result.unsupported.size() != 1U) {
      Fail("best-effort apply should record 1 unsupported parameter");
    }
    if (result.readback_rows.size() != 5U) {
      Fail("best-effort apply should capture readback rows for all requested settings");
    }

    bool saw_adjusted_frame_rate = false;
    bool saw_adjusted_exposure = false;
    bool saw_adjusted_gain = false;
    bool saw_adjusted_readback = false;
    bool saw_exposure_readback = false;
    bool saw_gain_readback = false;
    for (const auto& applied : result.applied) {
      if (applied.generic_key == "frame_rate") {
        if (!applied.adjusted) {
          Fail("expected frame_rate to be marked adjusted");
        }
        if (applied.applied_value != "240") {
          Fail("expected frame_rate to be clamped to 240");
        }
        saw_adjusted_frame_rate = true;
      }
      if (applied.generic_key == "exposure") {
        if (!applied.adjusted) {
          Fail("expected exposure to be marked adjusted");
        }
        if (applied.applied_value != "10000000") {
          Fail("expected exposure to be clamped to 10000000");
        }
        saw_adjusted_exposure = true;
      }
      if (applied.generic_key == "gain") {
        if (!applied.adjusted) {
          Fail("expected gain to be marked adjusted");
        }
        if (applied.applied_value != "0") {
          Fail("expected gain to be clamped to 0");
        }
        saw_adjusted_gain = true;
      }
    }
    for (const auto& row : result.readback_rows) {
      if (row.generic_key == "frame_rate") {
        if (!row.supported || !row.applied || !row.adjusted || row.actual_value != "240") {
          Fail("best-effort readback for frame_rate is incorrect");
        }
        saw_adjusted_readback = true;
      }
      if (row.generic_key == "exposure") {
        if (!row.supported || !row.applied || !row.adjusted || row.actual_value != "10000000") {
          Fail("best-effort readback for exposure is incorrect");
        }
        saw_exposure_readback = true;
      }
      if (row.generic_key == "gain") {
        if (!row.supported || !row.applied || !row.adjusted || row.actual_value != "0") {
          Fail("best-effort readback for gain is incorrect");
        }
        saw_gain_readback = true;
      }
    }
    if (!saw_adjusted_frame_rate) {
      Fail("expected adjusted frame_rate result entry");
    }
    if (!saw_adjusted_exposure) {
      Fail("expected adjusted exposure result entry");
    }
    if (!saw_adjusted_gain) {
      Fail("expected adjusted gain result entry");
    }
    if (!saw_adjusted_readback) {
      Fail("expected adjusted frame_rate readback entry");
    }
    if (!saw_exposure_readback) {
      Fail("expected adjusted exposure readback entry");
    }
    if (!saw_gain_readback) {
      Fail("expected adjusted gain readback entry");
    }

    const auto dumped = backend.DumpConfig();
    if (dumped.find("AcquisitionFrameRate") == dumped.end()) {
      Fail("expected backend to receive mapped AcquisitionFrameRate node");
    }
    if (dumped.find("ExposureTime") == dumped.end()) {
      Fail("expected backend to receive mapped ExposureTime node");
    }
    if (dumped.find("Gain") == dumped.end()) {
      Fail("expected backend to receive mapped Gain node");
    }
    if (dumped.find("PixelFormat") == dumped.end()) {
      Fail("expected backend to receive mapped PixelFormat node");
    }
  }

  std::cout << "real_apply_params_smoke: ok\n";
  return 0;
}
