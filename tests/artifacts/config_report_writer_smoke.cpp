#include "artifacts/config_report_writer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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
      fs::temp_directory_path() / ("labops-config-report-writer-smoke-" + std::to_string(now_ms));
  std::error_code ec;
  fs::remove_all(out_dir, ec);

  labops::core::schema::RunInfo run_info;
  run_info.run_id = "run-config-report-smoke";
  run_info.config.scenario_id = "config_report_smoke";
  run_info.config.backend = "real_stub";
  run_info.timestamps.started_at =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000LL));
  run_info.timestamps.finished_at = run_info.timestamps.started_at + std::chrono::milliseconds(250);

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
  if (!labops::artifacts::WriteConfigReportMarkdown(
          run_info, requested_params, apply_result,
          labops::backends::real_sdk::ParamApplyMode::kBestEffort,
          /*collection_error=*/"", out_dir, written_path, error)) {
    Fail(("WriteConfigReportMarkdown failed: " + error).c_str());
  }

  if (written_path != out_dir / "config_report.md") {
    Fail("unexpected written path for config report artifact");
  }

  std::ifstream input(written_path, std::ios::binary);
  if (!input) {
    Fail("failed to open written config_report.md");
  }
  const std::string markdown((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());

  AssertContains(markdown, "# Config Report");
  AssertContains(markdown, "| Status | Key | Node | Requested | Actual | Notes |");
  AssertContains(markdown, "✅ applied");
  AssertContains(markdown, "⚠ adjusted");
  AssertContains(markdown, "❌ unsupported");
  AssertContains(markdown, "- ✅ applied: 1");
  AssertContains(markdown, "- ⚠ adjusted: 1");
  AssertContains(markdown, "- ❌ unsupported: 1");
  AssertContains(markdown, "frame_rate");
  AssertContains(markdown, "trigger_mode");

  fs::remove_all(out_dir, ec);
  std::cout << "config_report_writer_smoke: ok\n";
  return 0;
}
