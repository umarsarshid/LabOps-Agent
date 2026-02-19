#include "events/emitter.hpp"

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

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

std::vector<std::string> ReadNonEmptyLines(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open events output");
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
  const fs::path out_dir =
      fs::temp_directory_path() / ("labops-emitter-smoke-" + std::to_string(now_ms));

  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  fs::path events_path;
  std::string error;
  labops::events::Emitter emitter(out_dir, events_path);

  if (!emitter.EmitConfigApplied(
          {
              .ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(1'000)),
              .run_id = "run-1",
              .scenario_id = "sim_baseline",
              .applied_params = {{"fps", "30"}, {"drop_percent", "20"}},
          },
          error)) {
    Fail("EmitConfigApplied failed: " + error);
  }

  if (!emitter.EmitStreamStarted(
          {
              .ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(2'000)),
              .run_id = "run-1",
              .scenario_id = "sim_baseline",
              .backend = "sim",
              .duration_ms = 1000,
              .fps = 30,
              .seed = 777,
              .soak_mode = false,
              .resume = false,
          },
          error)) {
    Fail("EmitStreamStarted failed: " + error);
  }

  if (!emitter.EmitFrameOutcome(
          {
              .ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(3'000)),
              .outcome = labops::events::Emitter::FrameOutcomeKind::kDropped,
              .run_id = "run-1",
              .frame_id = 42,
              .size_bytes = 0,
              .dropped = true,
              .reason = std::string("sim_fault_injection"),
          },
          error)) {
    Fail("EmitFrameOutcome(dropped) failed: " + error);
  }

  if (!emitter.EmitFrameOutcome(
          {
              .ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(4'000)),
              .outcome = labops::events::Emitter::FrameOutcomeKind::kTimeout,
              .run_id = "run-1",
              .frame_id = 43,
              .size_bytes = 0,
              .dropped = true,
              .reason = std::string("acquisition_timeout"),
          },
          error)) {
    Fail("EmitFrameOutcome(timeout) failed: " + error);
  }

  if (!emitter.EmitTransportAnomaly(
          {
              .ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(5'000)),
              .run_id = "run-1",
              .scenario_id = "sim_baseline",
              .heuristic_id = "resend_spike",
              .counter = "resends",
              .observed_value = 120,
              .threshold = 50,
              .summary = "Resend spike observed",
          },
          error)) {
    Fail("EmitTransportAnomaly failed: " + error);
  }

  const std::vector<std::string> lines = ReadNonEmptyLines(events_path);
  if (lines.size() != 5U) {
    Fail("expected exactly five event lines");
  }

  AssertContains(lines[0], "\"type\":\"CONFIG_APPLIED\"");
  AssertContains(lines[0], "\"run_id\":\"run-1\"");
  AssertContains(lines[0], "\"scenario_id\":\"sim_baseline\"");
  AssertContains(lines[0], "\"applied_count\":\"2\"");
  AssertContains(lines[0], "\"param.fps\":\"30\"");
  AssertContains(lines[0], "\"param.drop_percent\":\"20\"");

  AssertContains(lines[1], "\"type\":\"STREAM_STARTED\"");
  AssertContains(lines[1], "\"backend\":\"sim\"");
  AssertContains(lines[1], "\"duration_ms\":\"1000\"");
  AssertContains(lines[1], "\"seed\":\"777\"");

  AssertContains(lines[2], "\"type\":\"FRAME_DROPPED\"");
  AssertContains(lines[2], "\"frame_id\":\"42\"");
  AssertContains(lines[2], "\"reason\":\"sim_fault_injection\"");

  AssertContains(lines[3], "\"type\":\"FRAME_TIMEOUT\"");
  AssertContains(lines[3], "\"frame_id\":\"43\"");
  AssertContains(lines[3], "\"reason\":\"acquisition_timeout\"");

  AssertContains(lines[4], "\"type\":\"TRANSPORT_ANOMALY\"");
  AssertContains(lines[4], "\"heuristic_id\":\"resend_spike\"");
  AssertContains(lines[4], "\"counter\":\"resends\"");
  AssertContains(lines[4], "\"observed_value\":\"120\"");
  AssertContains(lines[4], "\"threshold\":\"50\"");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "emitter_smoke: ok\n";
  return 0;
}
