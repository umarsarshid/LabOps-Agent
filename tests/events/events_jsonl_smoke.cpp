#include "events/event_model.hpp"
#include "events/jsonl_writer.hpp"

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

} // namespace

int main() {
  using labops::events::Event;
  using labops::events::EventType;

  const fs::path out_dir = fs::temp_directory_path() / "labops-events-jsonl-smoke";
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  Event first;
  first.ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(1'000));
  first.type = EventType::kRunStarted;
  first.payload = {
      {"run_id", "run-1"},
      {"scenario_id", "sim_baseline"},
  };

  Event second;
  second.ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(2'000));
  second.type = EventType::kDeviceDisconnected;
  second.payload = {
      {"error", "device disconnected during acquisition"},
      {"reconnect_attempts_remaining", "2"},
  };

  Event third;
  third.ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(3'000));
  third.type = EventType::kTransportAnomaly;
  third.payload = {
      {"counter", "resends"},
      {"observed_value", "120"},
      {"threshold", "50"},
  };

  fs::path written_path;
  std::string error;
  if (!labops::events::AppendEventJsonl(first, out_dir, written_path, error)) {
    Fail("failed to append first event: " + error);
  }
  if (!labops::events::AppendEventJsonl(second, out_dir, written_path, error)) {
    Fail("failed to append second event: " + error);
  }
  if (!labops::events::AppendEventJsonl(third, out_dir, written_path, error)) {
    Fail("failed to append third event: " + error);
  }

  std::ifstream input(written_path, std::ios::binary);
  if (!input) {
    Fail("failed to open events.jsonl");
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }

  if (lines.size() != 3U) {
    Fail("expected exactly three event lines");
  }

  AssertContains(lines[0], "\"ts_utc\":\"1970-01-01T00:00:01.000Z\"");
  AssertContains(lines[0], "\"type\":\"run_started\"");
  AssertContains(lines[0], "\"run_id\":\"run-1\"");

  AssertContains(lines[1], "\"ts_utc\":\"1970-01-01T00:00:02.000Z\"");
  AssertContains(lines[1], "\"type\":\"DEVICE_DISCONNECTED\"");
  AssertContains(lines[1], "\"error\":\"device disconnected during acquisition\"");
  AssertContains(lines[1], "\"reconnect_attempts_remaining\":\"2\"");

  AssertContains(lines[2], "\"ts_utc\":\"1970-01-01T00:00:03.000Z\"");
  AssertContains(lines[2], "\"type\":\"TRANSPORT_ANOMALY\"");
  AssertContains(lines[2], "\"counter\":\"resends\"");
  AssertContains(lines[2], "\"observed_value\":\"120\"");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "events_jsonl_smoke: ok\n";
  return 0;
}
