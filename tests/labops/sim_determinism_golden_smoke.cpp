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

constexpr std::size_t kGoldenEventCount = 12;

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
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

bool ContainsLineType(const std::vector<std::string>& lines, std::string_view event_type) {
  const std::string needle = "\"type\":\"" + std::string(event_type) + "\"";
  for (const auto& line : lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
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

// Normalizes fields that are expected to differ between runs (`ts_utc`,
// `run_id`) so seeded determinism can be asserted against the remaining
// event contract (type + payload semantics).
std::string NormalizeDynamicFields(const std::string& line) {
  std::string normalized = line;

  auto replace_string_field = [&](std::string_view key, std::string_view replacement,
                                  bool required) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const std::size_t key_pos = normalized.find(needle);
    if (key_pos == std::string::npos) {
      return !required;
    }

    const std::size_t value_begin = key_pos + needle.size();
    const std::size_t value_end = normalized.find('"', value_begin);
    if (value_end == std::string::npos) {
      return false;
    }

    normalized.replace(value_begin, value_end - value_begin, replacement);
    return true;
  };

  if (!replace_string_field("ts_utc", "<ts>", true)) {
    Fail("failed to normalize ts_utc field");
  }
  if (!replace_string_field("run_id", "<run_id>", false)) {
    Fail("failed to normalize run_id field");
  }

  return normalized;
}

std::vector<std::string> RunScenario(const fs::path& scenario_path, const fs::path& out_dir) {
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
  const fs::path events_jsonl = bundle_dir / "events.jsonl";
  if (!fs::exists(events_jsonl)) {
    Fail("events.jsonl was not produced");
  }

  return ReadNonEmptyLines(events_jsonl);
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-sim-determinism-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "scenario.json";
  const fs::path out_a = temp_root / "out-a";
  const fs::path out_b = temp_root / "out-b";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  // Chosen knobs ensure both received and dropped frames show up in the first
  // K events, making determinism assertions meaningful.
  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    scenario_file << "{\n"
                  << "  \"name\": \"determinism\",\n"
                  << "  \"duration_ms\": 1200,\n"
                  << "  \"fps\": 25,\n"
                  << "  \"jitter_us\": 350,\n"
                  << "  \"seed\": 777,\n"
                  << "  \"frame_size_bytes\": 4096,\n"
                  << "  \"drop_every_n\": 4,\n"
                  << "  \"drop_percent\": 15,\n"
                  << "  \"burst_drop\": 2,\n"
                  << "  \"reorder\": 3\n"
                  << "}\n";
  }

  const auto first_run_lines = RunScenario(scenario_path, out_a);
  const auto second_run_lines = RunScenario(scenario_path, out_b);

  if (first_run_lines.size() < kGoldenEventCount || second_run_lines.size() < kGoldenEventCount) {
    Fail("trace does not contain enough events for determinism check");
  }

  if (!ContainsLineType(first_run_lines, "FRAME_RECEIVED")) {
    Fail("first run trace missing FRAME_RECEIVED");
  }
  if (!ContainsLineType(first_run_lines, "FRAME_DROPPED")) {
    Fail("first run trace missing FRAME_DROPPED");
  }

  for (std::size_t i = 0; i < kGoldenEventCount; ++i) {
    const std::string lhs = NormalizeDynamicFields(first_run_lines[i]);
    const std::string rhs = NormalizeDynamicFields(second_run_lines[i]);
    if (lhs != rhs) {
      std::cerr << "determinism mismatch at event index " << i << '\n';
      std::cerr << "first : " << lhs << '\n';
      std::cerr << "second: " << rhs << '\n';
      std::abort();
    }
  }

  fs::remove_all(temp_root, ec);
  std::cout << "sim_determinism_golden_smoke: ok\n";
  return 0;
}
