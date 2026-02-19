#include "labops/soak/checkpoint_store.hpp"

#include "core/fs_utils.hpp"
#include "core/json_dom.hpp"
#include "core/json_utils.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace labops::soak {

namespace {

using JsonValue = core::json::Value;
using JsonParser = core::json::Parser;
using JsonObject = JsonValue::Object;

bool ReadTextFile(const fs::path& path, std::string& contents, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "unable to read text file: " + path.string();
    return false;
  }

  contents.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return true;
}

bool IsInterruptedWriteSimulationEnabled() {
  const char* raw = std::getenv("LABOPS_SOAK_TEST_INTERRUPT_CHECKPOINT_WRITE");
  return raw != nullptr && std::string_view(raw) == "1";
}

bool WriteCheckpointTextAtomic(const fs::path& output_path, std::string_view text,
                               std::string& error) {
  if (!IsInterruptedWriteSimulationEnabled()) {
    return core::WriteTextFileAtomic(output_path, text, error);
  }

  // Test-only failure injection: simulate a process interruption after
  // checkpoint payload generation but before publish/rename.
  if (!core::EnsureParentDirectory(output_path, error)) {
    return false;
  }

  const fs::path temp_path = output_path.string() + ".tmp.interrupted";
  std::ofstream out_file(temp_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open simulated interrupted temp output file '" + temp_path.string() + "'";
    return false;
  }
  out_file << text;
  if (!out_file) {
    error =
        "failed while writing simulated interrupted temp output file '" + temp_path.string() + "'";
    return false;
  }
  error = "simulated interrupted checkpoint write before publish";
  return false;
}

const JsonValue* FindObjectField(const JsonObject& object, std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

bool ParseRequiredStringField(const JsonObject& object, std::string_view key, std::string& value,
                              std::string& error) {
  const JsonValue* field = FindObjectField(object, key);
  if (field == nullptr) {
    error = "checkpoint missing required field '" + std::string(key) + "'";
    return false;
  }
  if (field->type != JsonValue::Type::kString) {
    error = "checkpoint field '" + std::string(key) + "' must be a string";
    return false;
  }
  value = field->string_value;
  return true;
}

bool ParseOptionalStringField(const JsonObject& object, std::string_view key, std::string& value) {
  value.clear();
  const JsonValue* field = FindObjectField(object, key);
  if (field == nullptr || field->type != JsonValue::Type::kString) {
    return false;
  }
  value = field->string_value;
  return true;
}

bool ParseRequiredUnsignedField(const JsonObject& object, std::string_view key,
                                std::uint64_t& value, std::string& error) {
  const JsonValue* field = FindObjectField(object, key);
  if (field == nullptr) {
    error = "checkpoint missing required field '" + std::string(key) + "'";
    return false;
  }
  if (field->type != JsonValue::Type::kNumber) {
    error = "checkpoint field '" + std::string(key) + "' must be a non-negative integer";
    return false;
  }

  const double number = field->number_value;
  if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
      number > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    error = "checkpoint field '" + std::string(key) + "' must be a non-negative integer";
    return false;
  }
  value = static_cast<std::uint64_t>(number);
  return true;
}

std::optional<std::uint64_t> FindUnsignedJsonField(const std::string& text, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const std::size_t key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }

  const std::size_t colon_pos = text.find(':', key_pos + needle.size());
  if (colon_pos == std::string::npos) {
    return std::nullopt;
  }

  std::size_t value_pos = colon_pos + 1;
  while (value_pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[value_pos])) != 0) {
    ++value_pos;
  }

  const std::size_t start = value_pos;
  while (value_pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[value_pos])) != 0) {
    ++value_pos;
  }

  if (start == value_pos) {
    return std::nullopt;
  }

  std::uint64_t parsed = 0;
  const auto* begin = text.data() + static_cast<std::ptrdiff_t>(start);
  const auto* end = text.data() + static_cast<std::ptrdiff_t>(value_pos);
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

bool FindBoolJsonField(const std::string& text, std::string_view key, bool& value) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const std::size_t key_pos = text.find(needle);
  if (key_pos == std::string::npos) {
    return false;
  }

  const std::size_t colon_pos = text.find(':', key_pos + needle.size());
  if (colon_pos == std::string::npos) {
    return false;
  }

  std::size_t value_pos = colon_pos + 1;
  while (value_pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[value_pos])) != 0) {
    ++value_pos;
  }
  if (text.compare(value_pos, 4, "true") == 0) {
    value = true;
    return true;
  }
  if (text.compare(value_pos, 5, "false") == 0) {
    value = false;
    return true;
  }
  return false;
}

std::int64_t ToEpochMilliseconds(const std::chrono::system_clock::time_point ts) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
}

std::chrono::system_clock::time_point FromEpochMilliseconds(std::uint64_t epoch_ms) {
  return std::chrono::system_clock::time_point(
      std::chrono::milliseconds(static_cast<std::int64_t>(epoch_ms)));
}

} // namespace

const char* ToString(const CheckpointStatus status) {
  switch (status) {
  case CheckpointStatus::kRunning:
    return "running";
  case CheckpointStatus::kPaused:
    return "paused";
  case CheckpointStatus::kCompleted:
    return "completed";
  }
  return "running";
}

bool ParseCheckpointStatus(std::string_view text, CheckpointStatus& status) {
  if (text == "running") {
    status = CheckpointStatus::kRunning;
    return true;
  }
  if (text == "paused") {
    status = CheckpointStatus::kPaused;
    return true;
  }
  if (text == "completed") {
    status = CheckpointStatus::kCompleted;
    return true;
  }
  return false;
}

bool WriteCheckpointJson(const CheckpointState& state, const fs::path& output_path,
                         std::string& error) {
  error.clear();
  if (state.run_id.empty()) {
    error = "soak checkpoint run_id cannot be empty";
    return false;
  }
  if (output_path.empty()) {
    error = "soak checkpoint output path cannot be empty";
    return false;
  }

  const std::uint64_t total_ms =
      static_cast<std::uint64_t>(std::max<std::int64_t>(state.total_duration.count(), 0));
  const std::uint64_t completed_ms =
      static_cast<std::uint64_t>(std::max<std::int64_t>(state.completed_duration.count(), 0));
  const std::uint64_t remaining_ms = completed_ms >= total_ms ? 0U : (total_ms - completed_ms);

  std::ostringstream out;
  out << "{\n"
      << "  \"schema_version\": \"1.0\",\n"
      << "  \"mode\": \"soak\",\n"
      << "  \"status\": \"" << ToString(state.status) << "\",\n"
      << "  \"stop_reason\": \"" << core::EscapeJson(state.stop_reason) << "\",\n"
      << "  \"run_id\": \"" << core::EscapeJson(state.run_id) << "\",\n"
      << "  \"scenario_path\": \"" << core::EscapeJson(state.scenario_path.string()) << "\",\n"
      << "  \"bundle_dir\": \"" << core::EscapeJson(state.bundle_dir.string()) << "\",\n"
      << "  \"frame_cache_path\": \"" << core::EscapeJson(state.frame_cache_path.string())
      << "\",\n"
      << "  \"total_duration_ms\": " << total_ms << ",\n"
      << "  \"completed_duration_ms\": " << completed_ms << ",\n"
      << "  \"remaining_duration_ms\": " << remaining_ms << ",\n"
      << "  \"checkpoints_written\": " << state.checkpoints_written << ",\n"
      << "  \"frames_total\": " << state.frames_total << ",\n"
      << "  \"frames_received\": " << state.frames_received << ",\n"
      << "  \"frames_dropped\": " << state.frames_dropped << ",\n"
      << "  \"created_at_epoch_ms\": " << ToEpochMilliseconds(state.timestamps.created_at) << ",\n"
      << "  \"started_at_epoch_ms\": " << ToEpochMilliseconds(state.timestamps.started_at) << ",\n"
      << "  \"finished_at_epoch_ms\": " << ToEpochMilliseconds(state.timestamps.finished_at)
      << ",\n"
      << "  \"updated_at_epoch_ms\": " << ToEpochMilliseconds(state.updated_at) << ",\n"
      << "  \"resume_hint\": \"labops run " << core::EscapeJson(state.scenario_path.string())
      << " --soak --resume "
      << core::EscapeJson((state.bundle_dir / "soak_checkpoint.json").string()) << "\"\n"
      << "}\n";

  if (!WriteCheckpointTextAtomic(output_path, out.str(), error)) {
    error = "failed while writing soak checkpoint output '" + output_path.string() + "' (" + error +
            ")";
    return false;
  }

  return true;
}

bool WriteCheckpointArtifacts(const CheckpointState& state, fs::path& latest_checkpoint_path,
                              fs::path& history_checkpoint_path, std::string& error) {
  latest_checkpoint_path = state.bundle_dir / "soak_checkpoint.json";
  history_checkpoint_path = state.bundle_dir / "checkpoints" /
                            ("checkpoint_" + std::to_string(state.checkpoints_written) + ".json");

  if (!WriteCheckpointJson(state, latest_checkpoint_path, error)) {
    return false;
  }
  if (!WriteCheckpointJson(state, history_checkpoint_path, error)) {
    return false;
  }
  return true;
}

bool LoadCheckpoint(const fs::path& checkpoint_path, CheckpointState& state, std::string& error) {
  state = CheckpointState{};

  std::string text;
  if (!ReadTextFile(checkpoint_path, text, error)) {
    return false;
  }

  JsonValue root;
  std::string parse_error;
  JsonParser parser(text);
  if (!parser.Parse(root, parse_error)) {
    error = "invalid checkpoint JSON '" + checkpoint_path.string() + "': " + parse_error;
    return false;
  }
  if (root.type != JsonValue::Type::kObject) {
    error = "checkpoint root must be a JSON object";
    return false;
  }

  const JsonObject& object = root.object_value;
  std::string run_id;
  std::string scenario_path;
  std::string bundle_dir;
  std::string status_text;
  std::uint64_t total_duration_ms = 0;
  std::uint64_t completed_duration_ms = 0;
  std::uint64_t checkpoints_written = 0;
  std::uint64_t frames_total = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_dropped = 0;
  std::uint64_t created_at_epoch_ms = 0;
  std::uint64_t started_at_epoch_ms = 0;
  std::uint64_t finished_at_epoch_ms = 0;
  std::uint64_t updated_at_epoch_ms = 0;
  if (!ParseRequiredStringField(object, "run_id", run_id, error) ||
      !ParseRequiredStringField(object, "scenario_path", scenario_path, error) ||
      !ParseRequiredStringField(object, "bundle_dir", bundle_dir, error) ||
      !ParseRequiredStringField(object, "status", status_text, error) ||
      !ParseRequiredUnsignedField(object, "total_duration_ms", total_duration_ms, error) ||
      !ParseRequiredUnsignedField(object, "completed_duration_ms", completed_duration_ms, error) ||
      !ParseRequiredUnsignedField(object, "checkpoints_written", checkpoints_written, error) ||
      !ParseRequiredUnsignedField(object, "frames_total", frames_total, error) ||
      !ParseRequiredUnsignedField(object, "frames_received", frames_received, error) ||
      !ParseRequiredUnsignedField(object, "frames_dropped", frames_dropped, error) ||
      !ParseRequiredUnsignedField(object, "created_at_epoch_ms", created_at_epoch_ms, error) ||
      !ParseRequiredUnsignedField(object, "started_at_epoch_ms", started_at_epoch_ms, error) ||
      !ParseRequiredUnsignedField(object, "finished_at_epoch_ms", finished_at_epoch_ms, error) ||
      !ParseRequiredUnsignedField(object, "updated_at_epoch_ms", updated_at_epoch_ms, error)) {
    error = "checkpoint parse failed for '" + checkpoint_path.string() + "': " + error;
    return false;
  }

  CheckpointStatus status = CheckpointStatus::kRunning;
  if (!ParseCheckpointStatus(status_text, status)) {
    error = "checkpoint has unsupported status value: " + status_text;
    return false;
  }

  if (completed_duration_ms > total_duration_ms) {
    error = "checkpoint completed_duration_ms exceeds total_duration_ms";
    return false;
  }

  if (run_id.empty() || scenario_path.empty() || bundle_dir.empty()) {
    error = "checkpoint contains empty required identity fields";
    return false;
  }

  std::string frame_cache_path;
  ParseOptionalStringField(object, "frame_cache_path", frame_cache_path);

  state.run_id = run_id;
  state.scenario_path = fs::path(scenario_path);
  state.bundle_dir = fs::path(bundle_dir);
  state.frame_cache_path = !frame_cache_path.empty() ? fs::path(frame_cache_path)
                                                     : state.bundle_dir / "soak_frames.jsonl";
  state.total_duration = std::chrono::milliseconds(static_cast<std::int64_t>(total_duration_ms));
  state.completed_duration =
      std::chrono::milliseconds(static_cast<std::int64_t>(completed_duration_ms));
  state.checkpoints_written = checkpoints_written;
  state.frames_total = frames_total;
  state.frames_received = frames_received;
  state.frames_dropped = frames_dropped;
  state.timestamps.created_at = FromEpochMilliseconds(created_at_epoch_ms);
  state.timestamps.started_at = FromEpochMilliseconds(started_at_epoch_ms);
  state.timestamps.finished_at = FromEpochMilliseconds(finished_at_epoch_ms);
  state.updated_at = FromEpochMilliseconds(updated_at_epoch_ms);
  state.status = status;
  ParseOptionalStringField(object, "stop_reason", state.stop_reason);
  return true;
}

bool AppendFrameCache(const std::vector<backends::FrameSample>& frames,
                      const fs::path& frame_cache_path, std::string& error) {
  error.clear();
  if (frame_cache_path.empty()) {
    error = "frame cache path cannot be empty";
    return false;
  }

  std::error_code ec;
  fs::create_directories(frame_cache_path.parent_path(), ec);
  if (ec) {
    error = "failed to create frame cache directory '" + frame_cache_path.parent_path().string() +
            "': " + ec.message();
    return false;
  }

  std::ofstream out_file(frame_cache_path, std::ios::binary | std::ios::app);
  if (!out_file) {
    error = "failed to open frame cache file '" + frame_cache_path.string() + "'";
    return false;
  }

  for (const auto& frame : frames) {
    const auto ts_us =
        std::chrono::duration_cast<std::chrono::microseconds>(frame.timestamp.time_since_epoch())
            .count();
    const bool dropped = frame.dropped.has_value() && frame.dropped.value();
    out_file << "{\"frame_id\":" << frame.frame_id << ",\"ts_epoch_us\":" << ts_us
             << ",\"size_bytes\":" << frame.size_bytes
             << ",\"dropped\":" << (dropped ? "true" : "false") << "}\n";
  }

  if (!out_file) {
    error = "failed while appending frame cache file '" + frame_cache_path.string() + "'";
    return false;
  }

  return true;
}

bool LoadFrameCache(const fs::path& frame_cache_path, std::vector<backends::FrameSample>& frames,
                    std::string& error) {
  frames.clear();
  error.clear();

  std::error_code ec;
  if (!fs::exists(frame_cache_path, ec)) {
    return true;
  }
  if (ec) {
    error =
        "failed to access frame cache path '" + frame_cache_path.string() + "': " + ec.message();
    return false;
  }

  std::ifstream in_file(frame_cache_path, std::ios::binary);
  if (!in_file) {
    error = "failed to open frame cache file '" + frame_cache_path.string() + "'";
    return false;
  }

  std::string line;
  while (std::getline(in_file, line)) {
    if (line.empty()) {
      continue;
    }

    const auto frame_id = FindUnsignedJsonField(line, "frame_id");
    const auto ts_epoch_us = FindUnsignedJsonField(line, "ts_epoch_us");
    const auto size_bytes = FindUnsignedJsonField(line, "size_bytes");
    bool dropped = false;
    if (!frame_id.has_value() || !ts_epoch_us.has_value() || !size_bytes.has_value() ||
        !FindBoolJsonField(line, "dropped", dropped)) {
      error = "invalid frame cache line in '" + frame_cache_path.string() + "'";
      return false;
    }

    backends::FrameSample frame;
    frame.frame_id = frame_id.value();
    frame.timestamp = std::chrono::system_clock::time_point(
        std::chrono::microseconds(static_cast<std::int64_t>(ts_epoch_us.value())));
    frame.size_bytes = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(size_bytes.value(), std::numeric_limits<std::uint32_t>::max()));
    frame.dropped = dropped;
    frames.push_back(frame);
  }

  if (!in_file.eof() && in_file.fail()) {
    error = "failed while reading frame cache file '" + frame_cache_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::soak
