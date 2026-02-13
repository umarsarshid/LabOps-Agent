#include "artifacts/run_writer.hpp"
#include "core/schema/run_contract.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
  using labops::core::schema::RunInfo;

  // Fixed timestamps keep serialization assertions deterministic across runs.
  const auto now =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));

  RunInfo run_info;
  run_info.run_id = "run-1700000000000";
  run_info.config.scenario_id = "sim_baseline";
  run_info.config.backend = "sim";
  run_info.config.seed = 7;
  run_info.config.duration = std::chrono::minutes(10);
  run_info.timestamps.created_at = now;
  run_info.timestamps.started_at = now;
  run_info.timestamps.finished_at = now;

  const fs::path out_dir =
      fs::temp_directory_path() / ("labops-run-writer-smoke-" + run_info.run_id);
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  fs::path written_path;
  std::string error;
  if (!labops::artifacts::WriteRunJson(run_info, out_dir, written_path, error)) {
    Fail("WriteRunJson failed: " + error);
  }

  const fs::path expected_path = out_dir / "run.json";
  if (written_path != expected_path) {
    Fail("written path mismatch");
  }

  std::ifstream input(written_path, std::ios::binary);
  if (!input) {
    Fail("failed to open written run.json");
  }

  // Validate required contract fields rather than full-byte equality so the
  // test remains resilient to non-breaking formatting adjustments.
  std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  AssertContains(content, "\"run_id\":\"run-1700000000000\"");
  AssertContains(content, "\"scenario_id\":\"sim_baseline\"");
  AssertContains(content, "\"backend\":\"sim\"");
  AssertContains(content, "\"seed\":7");
  AssertContains(content, "\"duration_ms\":600000");
  AssertContains(content, "\"timestamps\":");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "run_writer_smoke: ok\n";
  return 0;
}
