#pragma once

#include "backends/camera_backend.hpp"
#include "core/schema/run_contract.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace labops::soak {

// Stable checkpoint lifecycle states used by soak-mode pause/resume logic.
enum class CheckpointStatus {
  kRunning,
  kPaused,
  kCompleted,
};

// Full persisted soak run state so a resumed run can continue deterministically
// without losing timing/frame evidence already collected.
struct CheckpointState {
  std::string run_id;
  std::filesystem::path scenario_path;
  std::filesystem::path bundle_dir;
  std::filesystem::path frame_cache_path;
  std::chrono::milliseconds total_duration{0};
  std::chrono::milliseconds completed_duration{0};
  std::uint64_t checkpoints_written = 0;
  std::uint64_t frames_total = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_dropped = 0;
  core::schema::RunTimestamps timestamps{};
  std::chrono::system_clock::time_point updated_at{};
  CheckpointStatus status = CheckpointStatus::kRunning;
  std::string stop_reason;
};

const char* ToString(CheckpointStatus status);
bool ParseCheckpointStatus(std::string_view text, CheckpointStatus& status);

// Writes one checkpoint JSON payload to an explicit path.
bool WriteCheckpointJson(const CheckpointState& state, const std::filesystem::path& output_path,
                         std::string& error);

// Writes both latest and history checkpoint artifacts.
bool WriteCheckpointArtifacts(const CheckpointState& state,
                              std::filesystem::path& latest_checkpoint_path,
                              std::filesystem::path& history_checkpoint_path, std::string& error);

// Loads checkpoint metadata used by soak resume flow.
bool LoadCheckpoint(const std::filesystem::path& checkpoint_path, CheckpointState& state,
                    std::string& error);

// Appends pulled frames to durable frame cache storage for soak resume.
bool AppendFrameCache(const std::vector<backends::FrameSample>& frames,
                      const std::filesystem::path& frame_cache_path, std::string& error);

// Loads all cached soak frames from prior checkpoints.
bool LoadFrameCache(const std::filesystem::path& frame_cache_path,
                    std::vector<backends::FrameSample>& frames, std::string& error);

} // namespace labops::soak
