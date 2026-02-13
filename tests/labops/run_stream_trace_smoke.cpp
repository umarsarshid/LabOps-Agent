#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
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

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

bool ContainsLineType(const std::vector<std::string>& lines, std::string_view event_type) {
  const std::string needle = "\"type\":\"" + std::string(event_type) + "\"";
  for (const auto& line : lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ReadNonEmptyLines(const fs::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + file_path.string());
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  if (!fs::exists(out_root)) {
    Fail("output root does not exist");
  }

  std::vector<fs::path> bundle_dirs;
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  if (bundle_dirs.size() != 1U) {
    Fail("expected exactly one run bundle directory");
  }
  return bundle_dirs.front();
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() / ("labops-run-trace-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "scenario.json";
  const fs::path out_dir = temp_root / "out";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  // Scenario knobs intentionally include deterministic drop/reorder behavior so
  // the generated trace contains both received and dropped frames.
  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    scenario_file << "{\n"
                  << "  \"name\": \"trace\",\n"
                  << "  \"duration_ms\": 500,\n"
                  << "  \"fps\": 30,\n"
                  << "  \"jitter_us\": 500,\n"
                  << "  \"seed\": 1234,\n"
                  << "  \"frame_size_bytes\": 2048,\n"
                  << "  \"drop_every_n\": 3,\n"
                  << "  \"drop_percent\": 20,\n"
                  << "  \"burst_drop\": 2,\n"
                  << "  \"reorder\": 4\n"
                  << "}\n";
  }

  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_dir.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 0) {
    Fail("labops run returned non-zero exit code");
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  const fs::path run_json = bundle_dir / "run.json";
  const fs::path scenario_json = bundle_dir / "scenario.json";
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  const fs::path metrics_csv = bundle_dir / "metrics.csv";
  const fs::path metrics_json = bundle_dir / "metrics.json";
  if (!fs::exists(run_json)) {
    Fail("run.json was not produced");
  }
  if (!fs::exists(scenario_json)) {
    Fail("scenario.json was not produced");
  }
  if (!fs::exists(events_jsonl)) {
    Fail("events.jsonl was not produced");
  }
  if (!fs::exists(metrics_csv)) {
    Fail("metrics.csv was not produced");
  }
  if (!fs::exists(metrics_json)) {
    Fail("metrics.json was not produced");
  }

  const auto lines = ReadNonEmptyLines(events_jsonl);
  if (lines.size() < 5U) {
    Fail("events trace is too short to be realistic");
  }

  if (!ContainsLineType(lines, "CONFIG_APPLIED")) {
    Fail("missing CONFIG_APPLIED event");
  }
  if (!ContainsLineType(lines, "STREAM_STARTED")) {
    Fail("missing STREAM_STARTED event");
  }
  if (!ContainsLineType(lines, "FRAME_RECEIVED")) {
    Fail("missing FRAME_RECEIVED event");
  }
  if (!ContainsLineType(lines, "FRAME_DROPPED")) {
    Fail("missing FRAME_DROPPED event");
  }
  if (!ContainsLineType(lines, "STREAM_STOPPED")) {
    Fail("missing STREAM_STOPPED event");
  }

  if (lines.front().find("\"type\":\"CONFIG_APPLIED\"") == std::string::npos) {
    Fail("first trace event must be CONFIG_APPLIED");
  }
  if (lines.front().find("\"param.fps\":\"30\"") == std::string::npos) {
    Fail("CONFIG_APPLIED payload missing param.fps");
  }
  if (lines.front().find("\"param.drop_percent\":\"20\"") == std::string::npos) {
    Fail("CONFIG_APPLIED payload missing param.drop_percent");
  }
  if (lines[1].find("\"type\":\"STREAM_STARTED\"") == std::string::npos) {
    Fail("second trace event must be STREAM_STARTED");
  }
  if (lines.back().find("\"type\":\"STREAM_STOPPED\"") == std::string::npos) {
    Fail("last trace event must be STREAM_STOPPED");
  }

  std::ifstream metrics_input(metrics_csv, std::ios::binary);
  if (!metrics_input) {
    Fail("failed to open metrics.csv");
  }
  const std::string metrics_content((std::istreambuf_iterator<char>(metrics_input)),
                                    std::istreambuf_iterator<char>());
  AssertContains(metrics_content, "metric,window_end_ms,window_ms,frames,fps");
  AssertContains(metrics_content, "avg_fps,");
  AssertContains(metrics_content, "drops_total");
  AssertContains(metrics_content, "drop_rate_percent");
  AssertContains(metrics_content, "inter_frame_interval_p95_us");
  AssertContains(metrics_content, "inter_frame_jitter_p95_us");

  std::ifstream metrics_json_input(metrics_json, std::ios::binary);
  if (!metrics_json_input) {
    Fail("failed to open metrics.json");
  }
  const std::string metrics_json_content((std::istreambuf_iterator<char>(metrics_json_input)),
                                         std::istreambuf_iterator<char>());
  AssertContains(metrics_json_content, "\"avg_fps\":");
  AssertContains(metrics_json_content, "\"drop_rate_percent\":");
  AssertContains(metrics_json_content, "\"rolling_fps\":[");

  fs::remove_all(temp_root, ec);
  std::cout << "run_stream_trace_smoke: ok\n";
  return 0;
}
