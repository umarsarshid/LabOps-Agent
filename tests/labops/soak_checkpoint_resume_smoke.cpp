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

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Fail("failed to open file: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

std::vector<std::string> ReadNonEmptyLines(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Fail("failed to open file: " + path.string());
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::size_t CountEventType(const std::vector<std::string>& lines, std::string_view event_type) {
  const std::string needle = "\"type\":\"" + std::string(event_type) + "\"";
  std::size_t count = 0;
  for (const auto& line : lines) {
    if (line.find(needle) != std::string::npos) {
      ++count;
    }
  }
  return count;
}

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  std::vector<fs::path> bundle_dirs;
  if (!fs::exists(out_root)) {
    Fail("output root does not exist");
  }

  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0U) == 0U) {
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
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-soak-resume-smoke-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "soak_scenario.json";
  const fs::path out_root = temp_root / "out";
  const fs::path stop_file = temp_root / "pause.request";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  // Scenario values are picked so a 400ms checkpoint interval produces exactly
  // 10 frames per chunk at 25 fps. This keeps pause/resume assertions stable.
  {
    std::ofstream scenario_file(scenario_path, std::ios::binary | std::ios::trunc);
    if (!scenario_file) {
      Fail("failed to open scenario file");
    }
    scenario_file << "{\n"
                  << "  \"name\": \"soak_resume\",\n"
                  << "  \"duration_ms\": 1200,\n"
                  << "  \"fps\": 25,\n"
                  << "  \"jitter_us\": 0,\n"
                  << "  \"seed\": 77,\n"
                  << "  \"frame_size_bytes\": 4096,\n"
                  << "  \"drop_every_n\": 0,\n"
                  << "  \"drop_percent\": 0,\n"
                  << "  \"burst_drop\": 0,\n"
                  << "  \"reorder\": 0\n"
                  << "}\n";
  }

  {
    std::ofstream stop_file_out(stop_file, std::ios::binary | std::ios::trunc);
    stop_file_out << "pause\n";
  }

  std::vector<std::string> pause_argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_root.string(),
      "--soak",
      "--checkpoint-interval-ms",
      "400",
      "--soak-stop-file",
      stop_file.string(),
  };
  std::vector<char*> pause_argv;
  pause_argv.reserve(pause_argv_storage.size());
  for (auto& arg : pause_argv_storage) {
    pause_argv.push_back(arg.data());
  }

  const int pause_exit = labops::cli::Dispatch(static_cast<int>(pause_argv.size()), pause_argv.data());
  if (pause_exit != 0) {
    Fail("soak pause run returned non-zero exit code");
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_root);
  const fs::path checkpoint_path = bundle_dir / "soak_checkpoint.json";
  const fs::path frame_cache_path = bundle_dir / "soak_frames.jsonl";
  const fs::path events_path = bundle_dir / "events.jsonl";
  const fs::path run_json_path = bundle_dir / "run.json";
  const fs::path manifest_path = bundle_dir / "bundle_manifest.json";

  if (!fs::exists(checkpoint_path)) {
    Fail("missing soak_checkpoint.json after pause run");
  }
  if (!fs::exists(frame_cache_path)) {
    Fail("missing soak_frames.jsonl after pause run");
  }
  if (!fs::exists(events_path)) {
    Fail("missing events.jsonl after pause run");
  }
  if (!fs::exists(run_json_path)) {
    Fail("missing run.json after pause run");
  }
  if (!fs::exists(manifest_path)) {
    Fail("missing bundle_manifest.json after pause run");
  }

  if (fs::exists(bundle_dir / "metrics.csv")) {
    Fail("pause run should not emit final metrics.csv yet");
  }

  const std::string paused_checkpoint = ReadFile(checkpoint_path);
  AssertContains(paused_checkpoint, "\"status\": \"paused\"");
  AssertContains(paused_checkpoint, "\"completed_duration_ms\": 400");
  AssertContains(paused_checkpoint, "\"remaining_duration_ms\": 800");

  fs::remove(stop_file, ec);

  std::vector<std::string> resume_argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--soak",
      "--resume",
      checkpoint_path.string(),
  };
  std::vector<char*> resume_argv;
  resume_argv.reserve(resume_argv_storage.size());
  for (auto& arg : resume_argv_storage) {
    resume_argv.push_back(arg.data());
  }

  const int resume_exit =
      labops::cli::Dispatch(static_cast<int>(resume_argv.size()), resume_argv.data());
  if (resume_exit != 0) {
    Fail("soak resume run returned non-zero exit code");
  }

  if (!fs::exists(bundle_dir / "metrics.csv")) {
    Fail("resume run missing metrics.csv");
  }
  if (!fs::exists(bundle_dir / "metrics.json")) {
    Fail("resume run missing metrics.json");
  }
  if (!fs::exists(bundle_dir / "summary.md")) {
    Fail("resume run missing summary.md");
  }
  if (!fs::exists(bundle_dir / "report.html")) {
    Fail("resume run missing report.html");
  }

  const std::string completed_checkpoint = ReadFile(checkpoint_path);
  AssertContains(completed_checkpoint, "\"status\": \"completed\"");
  AssertContains(completed_checkpoint, "\"completed_duration_ms\": 1200");
  AssertContains(completed_checkpoint, "\"remaining_duration_ms\": 0");

  const std::vector<std::string> events_lines = ReadNonEmptyLines(events_path);
  if (CountEventType(events_lines, "STREAM_STARTED") < 2U) {
    Fail("expected at least two STREAM_STARTED events across pause/resume");
  }
  if (CountEventType(events_lines, "STREAM_STOPPED") < 2U) {
    Fail("expected at least two STREAM_STOPPED events across pause/resume");
  }
  AssertContains(events_lines.back(), "\"frames_total\":\"30\"");

  fs::remove_all(temp_root, ec);
  std::cout << "soak_checkpoint_resume_smoke: ok\n";
  return 0;
}
