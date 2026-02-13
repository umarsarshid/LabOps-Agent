#include "labops/cli/router.hpp"

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

  const fs::path run_json = out_dir / "run.json";
  const fs::path events_jsonl = out_dir / "events.jsonl";
  if (!fs::exists(run_json)) {
    Fail("run.json was not produced");
  }
  if (!fs::exists(events_jsonl)) {
    Fail("events.jsonl was not produced");
  }

  const auto lines = ReadNonEmptyLines(events_jsonl);
  if (lines.size() < 4U) {
    Fail("events trace is too short to be realistic");
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

  if (lines.front().find("\"type\":\"STREAM_STARTED\"") == std::string::npos) {
    Fail("first trace event must be STREAM_STARTED");
  }
  if (lines.back().find("\"type\":\"STREAM_STOPPED\"") == std::string::npos) {
    Fail("last trace event must be STREAM_STOPPED");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "run_stream_trace_smoke: ok\n";
  return 0;
}
