#include "../common/assertions.hpp"
#include "../common/temp_dir.hpp"
#include "labops/soak/checkpoint_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

std::optional<std::string> ReadEnvVar(const char* name) {
  if (name == nullptr) {
    return std::nullopt;
  }
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

void SetEnvVar(const char* name, const char* value) {
#if defined(_WIN32)
  if (_putenv_s(name, value) != 0) {
    labops::tests::common::Fail("failed to set environment variable");
  }
#else
  if (setenv(name, value, 1) != 0) {
    labops::tests::common::Fail("failed to set environment variable");
  }
#endif
}

void UnsetEnvVar(const char* name) {
#if defined(_WIN32)
  if (_putenv_s(name, "") != 0) {
    labops::tests::common::Fail("failed to unset environment variable");
  }
#else
  if (unsetenv(name) != 0) {
    labops::tests::common::Fail("failed to unset environment variable");
  }
#endif
}

class ScopedEnvOverride {
public:
  ScopedEnvOverride(const char* name, const char* value)
      : name_(name), previous_(ReadEnvVar(name)) {
    SetEnvVar(name_, value);
  }

  ~ScopedEnvOverride() {
    if (previous_.has_value()) {
      SetEnvVar(name_, previous_->c_str());
      return;
    }
    UnsetEnvVar(name_);
  }

  ScopedEnvOverride(const ScopedEnvOverride&) = delete;
  ScopedEnvOverride& operator=(const ScopedEnvOverride&) = delete;

private:
  const char* name_ = "";
  std::optional<std::string> previous_;
};

labops::soak::CheckpointState BuildCheckpointState(const fs::path& temp_root) {
  const auto base_ts =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'000));
  labops::soak::CheckpointState state;
  state.run_id = "run-base";
  state.scenario_path = temp_root / "scenario.json";
  state.bundle_dir = temp_root / "bundle";
  state.frame_cache_path = state.bundle_dir / "soak_frames.jsonl";
  state.total_duration = std::chrono::milliseconds(3'000);
  state.completed_duration = std::chrono::milliseconds(1'000);
  state.checkpoints_written = 4;
  state.frames_total = 75;
  state.frames_received = 70;
  state.frames_dropped = 5;
  state.timestamps.created_at = base_ts;
  state.timestamps.started_at = base_ts + std::chrono::milliseconds(20);
  state.timestamps.finished_at = base_ts + std::chrono::milliseconds(1'000);
  state.updated_at = base_ts + std::chrono::milliseconds(1'005);
  state.status = labops::soak::CheckpointStatus::kPaused;
  state.stop_reason = "pause_request";
  return state;
}

void WriteMalformedCheckpoint(const fs::path& path) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    labops::tests::common::Fail("failed to create malformed checkpoint fixture");
  }
  out << "{\n"
      << "  \"run_id\": \"broken\"\n";
}

} // namespace

int main() {
  using labops::tests::common::AssertContains;
  using labops::tests::common::AssertNotContains;
  using labops::tests::common::CreateUniqueTempDir;
  using labops::tests::common::ReadFileToString;
  using labops::tests::common::RemovePathBestEffort;

  const fs::path temp_root = CreateUniqueTempDir("labops-soak-resilience");
  const fs::path bundle_dir = temp_root / "bundle";
  const fs::path checkpoint_path = bundle_dir / "soak_checkpoint.json";

  std::error_code ec;
  fs::create_directories(bundle_dir, ec);
  if (ec) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("failed to create bundle directory");
  }

  std::string error;
  const labops::soak::CheckpointState base_state = BuildCheckpointState(temp_root);
  if (!labops::soak::WriteCheckpointJson(base_state, checkpoint_path, error)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("failed to write baseline checkpoint: " + error);
  }

  {
    // Simulate process interruption before atomic publish. The original
    // checkpoint file must remain intact.
    ScopedEnvOverride interrupt_write("LABOPS_SOAK_TEST_INTERRUPT_CHECKPOINT_WRITE", "1");
    labops::soak::CheckpointState updated_state = base_state;
    updated_state.run_id = "run-after-interrupt";
    updated_state.completed_duration = std::chrono::milliseconds(2'000);
    updated_state.checkpoints_written = 5;
    if (labops::soak::WriteCheckpointJson(updated_state, checkpoint_path, error)) {
      RemovePathBestEffort(temp_root);
      labops::tests::common::Fail("expected interrupted-write simulation to fail");
    }
    AssertContains(error, "simulated interrupted checkpoint write before publish");
  }

  labops::soak::CheckpointState loaded_after_interrupt;
  if (!labops::soak::LoadCheckpoint(checkpoint_path, loaded_after_interrupt, error)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("failed to load checkpoint after interrupted-write simulation: " +
                                error);
  }
  if (loaded_after_interrupt.run_id != base_state.run_id ||
      loaded_after_interrupt.completed_duration != base_state.completed_duration ||
      loaded_after_interrupt.checkpoints_written != base_state.checkpoints_written) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail(
        "checkpoint contents changed after interrupted-write simulation");
  }
  const std::string persisted_text = ReadFileToString(checkpoint_path);
  AssertContains(persisted_text, "\"run_id\": \"run-base\"");
  AssertNotContains(persisted_text, "run-after-interrupt");

  // Malformed checkpoint should fail with parser diagnostics, and writing a
  // new valid checkpoint to the same path should recover cleanly.
  const fs::path malformed_checkpoint = bundle_dir / "soak_checkpoint_malformed.json";
  WriteMalformedCheckpoint(malformed_checkpoint);
  labops::soak::CheckpointState ignored_state;
  if (labops::soak::LoadCheckpoint(malformed_checkpoint, ignored_state, error)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("expected malformed checkpoint load to fail");
  }
  AssertContains(error, "invalid checkpoint JSON");

  if (!labops::soak::WriteCheckpointJson(base_state, malformed_checkpoint, error)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("failed to recover malformed checkpoint path: " + error);
  }
  labops::soak::CheckpointState recovered_state;
  if (!labops::soak::LoadCheckpoint(malformed_checkpoint, recovered_state, error)) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("failed to load recovered checkpoint path: " + error);
  }
  if (recovered_state.run_id != base_state.run_id ||
      recovered_state.status != base_state.status ||
      recovered_state.total_duration != base_state.total_duration) {
    RemovePathBestEffort(temp_root);
    labops::tests::common::Fail("recovered checkpoint state does not match expected values");
  }

  RemovePathBestEffort(temp_root);
  return 0;
}
