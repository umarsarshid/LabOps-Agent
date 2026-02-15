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
                         ApplyParamInput{"pixel_format", "mono8"},
                         ApplyParamInput{"unknown_knob", "1"},
                     },
                     ParamApplyMode::kBestEffort, result, error)) {
      Fail("best-effort apply unexpectedly failed: " + error);
    }

    if (result.applied.size() != 2U) {
      Fail("best-effort apply should keep 2 supported parameters");
    }
    if (result.unsupported.size() != 1U) {
      Fail("best-effort apply should record 1 unsupported parameter");
    }

    bool saw_adjusted_frame_rate = false;
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
    }
    if (!saw_adjusted_frame_rate) {
      Fail("expected adjusted frame_rate result entry");
    }

    const auto dumped = backend.DumpConfig();
    if (dumped.find("AcquisitionFrameRate") == dumped.end()) {
      Fail("expected backend to receive mapped AcquisitionFrameRate node");
    }
    if (dumped.find("PixelFormat") == dumped.end()) {
      Fail("expected backend to receive mapped PixelFormat node");
    }
  }

  std::cout << "real_apply_params_smoke: ok\n";
  return 0;
}
