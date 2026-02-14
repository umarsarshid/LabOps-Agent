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

std::string ReadFile(const fs::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + file_path.string());
  }

  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-kb-draft-smoke-" + std::to_string(now_ms));
  const fs::path run_dir = temp_root / "run-123456";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(run_dir, ec);
  if (ec) {
    Fail("failed to create temp run dir");
  }

  // Seed a representative engineer packet to validate section extraction into
  // the KB draft template.
  {
    std::ofstream packet_file(run_dir / "engineer_packet.md", std::ios::binary | std::ios::trunc);
    if (!packet_file) {
      Fail("failed to open engineer_packet.md");
    }

    packet_file << "# Engineer Packet\n\n"
                << "## Run Context\n\n"
                << "- session_id: `session-42`\n"
                << "- scenario_id: `dropped_frames`\n"
                << "- symptom: `dropped_frames`\n"
                << "- baseline_scenario: `scenarios/sim_baseline.json`\n"
                << "- baseline_bundle: `baselines/sim_baseline`\n"
                << "- stop_reason: `single_variable_flip`\n"
                << "- stop_explanation: isolated fps mutation\n\n"
                << "## Repro Steps\n\n"
                << "1. Validate scenario.\n"
                << "2. Run baseline.\n"
                << "3. Toggle FPS and rerun.\n\n"
                << "## What We Ruled Out\n\n"
                << "- ROI change alone did not reproduce failure.\n\n"
                << "## Ranked Hypotheses + Evidence Links\n\n"
                << "1. [`h_fps`] score=4 status=`supported` variable=`camera.fps`\n"
                << "   - statement: camera fps regression reproduces drop burst.\n"
                << "   - support_count: 2, contradiction_count: 0\n\n"
                << "2. [`h_roi`] score=-1 status=`rejected` variable=`camera.roi.width`\n"
                << "   - statement: ROI width alone causes drops.\n";
  }

  {
    std::ofstream summary_file(run_dir / "summary.md", std::ios::binary | std::ios::trunc);
    summary_file << "# Summary\n";
  }

  std::vector<std::string> argv_storage = {
      "labops", "kb", "draft", "--run", run_dir.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 0) {
    Fail("labops kb draft returned non-zero exit code");
  }

  const fs::path kb_draft_path = run_dir / "kb_draft.md";
  if (!fs::exists(kb_draft_path)) {
    Fail("kb_draft.md was not produced");
  }

  const std::string kb_text = ReadFile(kb_draft_path);
  AssertContains(kb_text, "# KB Draft:");
  AssertContains(kb_text, "scenario_id: `dropped_frames`");
  AssertContains(kb_text, "camera fps regression reproduces drop burst.");
  AssertContains(kb_text, "ROI change alone did not reproduce failure.");
  AssertContains(kb_text, "engineer_packet:");
  AssertContains(kb_text, "summary:");

  fs::remove_all(temp_root, ec);
  std::cout << "kb_draft_from_run_folder_smoke: ok\n";
  return 0;
}
