#include "artifacts/camera_config_writer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(std::string_view haystack, std::string_view needle) {
  if (haystack.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual content:\n" << haystack << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path out_dir =
      fs::temp_directory_path() / ("labops-camera-config-writer-smoke-" + std::to_string(now_ms));
  std::error_code ec;
  fs::remove_all(out_dir, ec);

  labops::core::schema::RunInfo run_info;
  run_info.run_id = "run-camera-config-smoke";
  run_info.config.scenario_id = "camera_config_smoke";
  run_info.config.backend = "real_stub";
  run_info.real_device = labops::core::schema::RealDeviceMetadata{
      .model = "SprintCam",
      .serial = "SN-2000",
      .transport = "usb",
      .user_id = std::optional<std::string>("Secondary"),
      .firmware_version = std::optional<std::string>("4.0.0"),
      .sdk_version = std::optional<std::string>("21.1.8"),
  };

  labops::backends::BackendConfig backend_dump = {
      {"device.selector", "serial:SN-2000"},
      {"device.index", "0"},
      {"device.ip", "10.0.0.21"},
      {"connected", "false"},
      {"running", "false"},
  };

  std::vector<labops::backends::real_sdk::ApplyParamInput> requested_params = {
      {.generic_key = "frame_rate", .requested_value = "1000"},
      {.generic_key = "pixel_format", .requested_value = "mono8"},
      {.generic_key = "trigger_mode", .requested_value = "on"},
  };

  labops::backends::real_sdk::ApplyParamsResult apply_result;
  apply_result.readback_rows.push_back({
      .generic_key = "frame_rate",
      .node_name = "AcquisitionFrameRate",
      .requested_value = "1000",
      .actual_value = "240",
      .supported = true,
      .applied = true,
      .adjusted = true,
      .reason = "clamped from 1000 to 240",
  });
  apply_result.readback_rows.push_back({
      .generic_key = "pixel_format",
      .node_name = "PixelFormat",
      .requested_value = "mono8",
      .actual_value = "mono8",
      .supported = true,
      .applied = true,
      .adjusted = false,
      .reason = "",
  });
  apply_result.readback_rows.push_back({
      .generic_key = "trigger_mode",
      .node_name = "TriggerMode",
      .requested_value = "on",
      .actual_value = "",
      .supported = true,
      .applied = false,
      .adjusted = false,
      .reason = "value 'on' is not supported for key 'TriggerMode'",
  });

  fs::path written_path;
  std::string error;
  if (!labops::artifacts::WriteCameraConfigJson(run_info, backend_dump, requested_params,
                                                apply_result,
                                                labops::backends::real_sdk::ParamApplyMode::
                                                    kBestEffort,
                                                /*collection_error=*/"", out_dir, written_path,
                                                error)) {
    Fail(("WriteCameraConfigJson failed: " + error).c_str());
  }

  if (written_path != out_dir / "camera_config.json") {
    Fail("unexpected written path for camera config artifact");
  }

  std::ifstream input(written_path, std::ios::binary);
  if (!input) {
    Fail("failed to open written camera_config.json");
  }
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

  AssertContains(json, "\"schema_version\":\"1.0\"");
  AssertContains(json, "\"identity\":{");
  AssertContains(json, "\"model\":\"SprintCam\"");
  AssertContains(json, "\"serial\":\"SN-2000\"");
  AssertContains(json, "\"selector\":\"serial:SN-2000\"");
  AssertContains(json, "\"generic_key\":\"frame_rate\"");
  AssertContains(json, "\"requested\":\"1000\"");
  AssertContains(json, "\"actual\":\"240\"");
  AssertContains(json, "\"missing_keys\":[");
  AssertContains(json, "\"unsupported_keys\":[\"trigger_mode\"]");
  AssertContains(json, "\"collection_error\":null");
  AssertContains(json, "\"backend_dump\":{");

  fs::remove_all(out_dir, ec);
  std::cout << "camera_config_writer_smoke: ok\n";
  return 0;
}
