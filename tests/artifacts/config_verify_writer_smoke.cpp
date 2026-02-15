#include "artifacts/config_verify_writer.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

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

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path out_dir =
      fs::temp_directory_path() / ("labops-config-verify-writer-smoke-" + std::to_string(now_ms));

  std::error_code ec;
  fs::remove_all(out_dir, ec);
  fs::create_directories(out_dir, ec);
  if (ec) {
    Fail("failed to create temp output directory");
  }

  labops::core::schema::RunInfo run_info;
  run_info.run_id = "run-config-verify";
  run_info.config.scenario_id = "real_readback_smoke";
  run_info.config.backend = "real_stub";
  run_info.config.seed = 42;
  run_info.config.duration = std::chrono::milliseconds(500);

  labops::backends::real_sdk::ApplyParamsResult result;
  result.readback_rows.push_back({.generic_key = "frame_rate",
                                  .node_name = "AcquisitionFrameRate",
                                  .requested_value = "1000",
                                  .actual_value = "240",
                                  .supported = true,
                                  .applied = true,
                                  .adjusted = true,
                                  .reason = "clamped from 1000 to 240"});
  result.readback_rows.push_back({.generic_key = "trigger_source",
                                  .node_name = "TriggerSource",
                                  .requested_value = "line9",
                                  .actual_value = "",
                                  .supported = false,
                                  .applied = false,
                                  .adjusted = false,
                                  .reason = "mapped SDK node 'TriggerSource' is not available"});

  fs::path written_path;
  std::string error;
  if (!labops::artifacts::WriteConfigVerifyJson(
          run_info, result, labops::backends::real_sdk::ParamApplyMode::kBestEffort, out_dir,
          written_path, error)) {
    Fail("WriteConfigVerifyJson failed: " + error);
  }

  if (written_path != out_dir / "config_verify.json") {
    Fail("unexpected written path for config_verify artifact");
  }

  std::ifstream input(written_path, std::ios::binary);
  if (!input) {
    Fail("failed to open written config_verify.json");
  }
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

  AssertContains(text, "\"schema_version\":\"1.0\"");
  AssertContains(text, "\"run_id\":\"run-config-verify\"");
  AssertContains(text, "\"apply_mode\":\"best_effort\"");
  AssertContains(text, "\"requested_count\":2");
  AssertContains(text, "\"supported_count\":1");
  AssertContains(text, "\"unsupported_count\":1");
  AssertContains(text, "\"adjusted_count\":1");
  AssertContains(text, "\"generic_key\":\"frame_rate\"");
  AssertContains(text, "\"requested\":\"1000\"");
  AssertContains(text, "\"actual\":\"240\"");
  AssertContains(text, "\"supported\":true");
  AssertContains(text, "\"applied\":true");
  AssertContains(text, "\"generic_key\":\"trigger_source\"");
  AssertContains(text, "\"actual\":null");
  AssertContains(text, "\"supported\":false");
  AssertContains(text, "\"applied\":false");

  fs::remove_all(out_dir, ec);
  std::cout << "config_verify_writer_smoke: ok\n";
  return 0;
}
