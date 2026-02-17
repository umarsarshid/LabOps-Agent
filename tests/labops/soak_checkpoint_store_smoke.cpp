#include "labops/soak/checkpoint_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
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

void Assert(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-soak-store-smoke-" + std::to_string(now_ms));
  const fs::path bundle_dir = temp_root / "bundle";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(bundle_dir, ec);
  if (ec) {
    Fail("failed to create temporary test directory");
  }

  const auto base_ts =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));

  labops::soak::CheckpointState state;
  state.run_id = "run-smoke";
  state.scenario_path = temp_root / "scenario.json";
  state.bundle_dir = bundle_dir;
  state.frame_cache_path = bundle_dir / "soak_frames.jsonl";
  state.total_duration = std::chrono::milliseconds(3'000);
  state.completed_duration = std::chrono::milliseconds(1'000);
  state.checkpoints_written = 2;
  state.frames_total = 45;
  state.frames_received = 40;
  state.frames_dropped = 5;
  state.timestamps.created_at = base_ts;
  state.timestamps.started_at = base_ts + std::chrono::milliseconds(10);
  state.timestamps.finished_at = base_ts + std::chrono::milliseconds(1'000);
  state.updated_at = base_ts + std::chrono::milliseconds(1'001);
  state.status = labops::soak::CheckpointStatus::kPaused;
  state.stop_reason = "stop_file_detected";

  fs::path latest_path;
  fs::path history_path;
  std::string error;
  if (!labops::soak::WriteCheckpointArtifacts(state, latest_path, history_path, error)) {
    Fail("failed to write checkpoint artifacts: " + error);
  }

  Assert(fs::exists(latest_path), "latest soak checkpoint file was not created");
  Assert(fs::exists(history_path), "history soak checkpoint file was not created");

  labops::soak::CheckpointState loaded;
  if (!labops::soak::LoadCheckpoint(latest_path, loaded, error)) {
    Fail("failed to load written checkpoint: " + error);
  }

  Assert(loaded.run_id == state.run_id, "loaded run_id mismatch");
  Assert(loaded.status == labops::soak::CheckpointStatus::kPaused, "loaded status mismatch");
  Assert(loaded.stop_reason == state.stop_reason, "loaded stop reason mismatch");
  Assert(loaded.completed_duration == state.completed_duration,
         "loaded completed_duration mismatch");

  std::vector<labops::backends::FrameSample> first_batch;
  labops::backends::FrameSample first;
  first.frame_id = 1;
  first.timestamp = base_ts + std::chrono::microseconds(1);
  first.size_bytes = 4096;
  first.dropped = false;
  first_batch.push_back(first);

  std::vector<labops::backends::FrameSample> second_batch;
  labops::backends::FrameSample second;
  second.frame_id = 2;
  second.timestamp = base_ts + std::chrono::microseconds(2);
  second.size_bytes = 0;
  second.dropped = true;
  second_batch.push_back(second);

  if (!labops::soak::AppendFrameCache(first_batch, state.frame_cache_path, error)) {
    Fail("failed to append first frame cache batch: " + error);
  }
  if (!labops::soak::AppendFrameCache(second_batch, state.frame_cache_path, error)) {
    Fail("failed to append second frame cache batch: " + error);
  }

  std::vector<labops::backends::FrameSample> loaded_frames;
  if (!labops::soak::LoadFrameCache(state.frame_cache_path, loaded_frames, error)) {
    Fail("failed to load frame cache: " + error);
  }

  Assert(loaded_frames.size() == 2U, "loaded frame cache size mismatch");
  Assert(loaded_frames[0].frame_id == 1U, "first frame id mismatch");
  Assert(loaded_frames[1].frame_id == 2U, "second frame id mismatch");
  Assert(loaded_frames[1].dropped.has_value() && loaded_frames[1].dropped.value(),
         "second frame dropped flag mismatch");

  fs::remove_all(temp_root, ec);
  std::cout << "soak_checkpoint_store_smoke: ok\n";
  return 0;
}
