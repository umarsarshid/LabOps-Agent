#include "labops/cli/router.hpp"

#include "artifacts/run_writer.hpp"
#include "backends/camera_backend.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "core/schema/run_contract.hpp"
#include "events/event_model.hpp"
#include "events/jsonl_writer.hpp"
#include "metrics/csv_writer.hpp"
#include "metrics/fps.hpp"

#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace labops::cli {

namespace {

// Exit code contract is centralized here so every subcommand remains
// consistent and automation can rely on deterministic behavior.
constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

// One usage text source avoids divergence between help and error paths.
void PrintUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops run <scenario.json> [--out <dir>]\n"
      << "  labops validate <scenario.json>\n"
      << "  labops version\n";
}

// Early-stage scenario preflight checks. These are intentionally lightweight
// and filesystem-focused; full schema validation belongs in the scenarios
// module as that gets implemented.
bool ValidateScenarioPath(const std::string& scenario_path, std::string& error) {
  if (scenario_path.empty()) {
    error = "scenario path cannot be empty";
    return false;
  }

  fs::path path(scenario_path);
  std::error_code ec;

  if (!fs::exists(path, ec) || ec) {
    error = "scenario file not found: " + scenario_path;
    return false;
  }

  if (!fs::is_regular_file(path, ec) || ec) {
    error = "scenario path must point to a regular file: " + scenario_path;
    return false;
  }

  if (path.extension() != ".json") {
    error = "scenario file must use .json extension: " + scenario_path;
    return false;
  }

  std::ifstream file(path);
  if (!file) {
    error = "unable to open scenario file: " + scenario_path;
    return false;
  }

  if (file.peek() == std::ifstream::traits_type::eof()) {
    error = "scenario file is empty: " + scenario_path;
    return false;
  }

  return true;
}

int CommandVersion(const std::vector<std::string_view>& args) {
  if (!args.empty()) {
    std::cerr << "error: version does not accept arguments\n";
    return kExitUsage;
  }

  std::cout << "labops 0.1.0\n";
  return kExitSuccess;
}

int CommandValidate(const std::vector<std::string_view>& args) {
  if (args.size() != 1) {
    std::cerr << "error: validate requires exactly 1 argument: <scenario.json>\n";
    return kExitUsage;
  }

  std::string error;
  const std::string scenario_path(args.front());
  if (!ValidateScenarioPath(scenario_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  std::cout << "valid: " << scenario_path << '\n';
  return kExitSuccess;
}

struct RunOptions {
  std::string scenario_path;
  fs::path output_dir = "out";
};

struct RunPlan {
  backends::sim::SimScenarioConfig sim_config;
  std::chrono::milliseconds duration{1'000};
};

// Parse `run` args with an explicit contract:
// - one scenario path
// - optional `--out <dir>`
// Any unknown flags or duplicate positional args are treated as usage errors.
bool ParseRunOptions(const std::vector<std::string_view>& args, RunOptions& options,
                     std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--out") {
      if (i + 1 >= args.size()) {
        error = "missing value for --out";
        return false;
      }
      options.output_dir = fs::path(args[i + 1]);
      ++i;
      continue;
    }

    if (!token.empty() && token.front() == '-') {
      error = "unknown option: " + std::string(token);
      return false;
    }

    if (!options.scenario_path.empty()) {
      error = "run accepts exactly 1 scenario path";
      return false;
    }

    options.scenario_path = std::string(token);
  }

  if (options.scenario_path.empty()) {
    error = "run requires exactly 1 argument: <scenario.json>";
    return false;
  }

  return true;
}

bool ReadTextFile(const std::string& path, std::string& contents, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "unable to read scenario file: " + path;
    return false;
  }

  contents.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return true;
}

// Lightweight numeric field extraction for milestone wiring.
// This intentionally supports only unsigned integer fields.
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

bool LoadRunPlanFromScenario(const std::string& scenario_path, RunPlan& plan, std::string& error) {
  std::string scenario_text;
  if (!ReadTextFile(scenario_path, scenario_text, error)) {
    return false;
  }

  auto assign_u32 = [&](std::string_view key, std::uint32_t& target,
                        std::uint32_t max_value = std::numeric_limits<std::uint32_t>::max()) {
    const auto value = FindUnsignedJsonField(scenario_text, key);
    if (!value.has_value()) {
      return true;
    }
    if (value.value() > static_cast<std::uint64_t>(max_value)) {
      error = "scenario field out of range for key: " + std::string(key);
      return false;
    }
    target = static_cast<std::uint32_t>(value.value());
    return true;
  };

  auto assign_u64 = [&](std::string_view key, std::uint64_t& target) {
    const auto value = FindUnsignedJsonField(scenario_text, key);
    if (!value.has_value()) {
      return true;
    }
    target = value.value();
    return true;
  };

  if (const auto duration_ms = FindUnsignedJsonField(scenario_text, "duration_ms");
      duration_ms.has_value()) {
    if (duration_ms.value() == 0U) {
      error = "scenario duration_ms must be greater than 0";
      return false;
    }
    plan.duration = std::chrono::milliseconds(static_cast<std::int64_t>(duration_ms.value()));
  } else if (const auto duration_s = FindUnsignedJsonField(scenario_text, "duration_s");
             duration_s.has_value()) {
    if (duration_s.value() == 0U) {
      error = "scenario duration_s must be greater than 0";
      return false;
    }
    plan.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(static_cast<std::int64_t>(duration_s.value())));
  }

  if (!assign_u32("fps", plan.sim_config.fps)) {
    return false;
  }
  if (!assign_u32("jitter_us", plan.sim_config.jitter_us)) {
    return false;
  }
  if (!assign_u64("seed", plan.sim_config.seed)) {
    return false;
  }
  if (!assign_u32("frame_size_bytes", plan.sim_config.frame_size_bytes)) {
    return false;
  }
  if (!assign_u32("drop_every_n", plan.sim_config.drop_every_n)) {
    return false;
  }
  if (!assign_u32("drop_percent", plan.sim_config.faults.drop_percent, 100U)) {
    return false;
  }
  if (!assign_u32("burst_drop", plan.sim_config.faults.burst_drop)) {
    return false;
  }
  if (!assign_u32("reorder", plan.sim_config.faults.reorder)) {
    return false;
  }

  return true;
}

// Generate a stable-enough run identifier for early artifact wiring. This is
// intentionally simple and timestamp-based until a dedicated ID module exists.
std::string MakeRunId(std::chrono::system_clock::time_point now) {
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return "run-" + std::to_string(millis);
}

core::schema::RunInfo BuildRunInfo(const RunOptions& options, const RunPlan& run_plan,
                                   std::chrono::system_clock::time_point created_at) {
  core::schema::RunInfo run_info;
  run_info.run_id = MakeRunId(created_at);
  run_info.config.scenario_id = fs::path(options.scenario_path).stem().string();
  run_info.config.backend = "sim";
  run_info.config.seed = run_plan.sim_config.seed;
  run_info.config.duration = run_plan.duration;
  run_info.timestamps.created_at = created_at;
  run_info.timestamps.started_at = created_at;
  run_info.timestamps.finished_at = created_at;
  return run_info;
}

bool AppendTraceEvent(events::EventType type, std::chrono::system_clock::time_point ts,
                      std::map<std::string, std::string> payload,
                      const fs::path& output_dir,
                      fs::path& events_path,
                      std::string& error) {
  events::Event event;
  event.ts = ts;
  event.type = type;
  event.payload = std::move(payload);
  return events::AppendEventJsonl(event, output_dir, events_path, error);
}

int CommandRun(const std::vector<std::string_view>& args) {
  RunOptions options;
  std::string error;
  if (!ParseRunOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  if (!ValidateScenarioPath(options.scenario_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  RunPlan run_plan;
  if (!LoadRunPlanFromScenario(options.scenario_path, run_plan, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  const auto created_at = std::chrono::system_clock::now();
  core::schema::RunInfo run_info = BuildRunInfo(options, run_plan, created_at);

  std::unique_ptr<backends::ICameraBackend> backend =
      std::make_unique<backends::sim::SimCameraBackend>();

  if (!backend->Connect(error)) {
    std::cerr << "error: backend connect failed: " << error << '\n';
    return kExitFailure;
  }

  if (!backends::sim::ApplyScenarioConfig(*backend, run_plan.sim_config, error)) {
    std::cerr << "error: backend config failed: " << error << '\n';
    return kExitFailure;
  }

  if (!backend->Start(error)) {
    std::cerr << "error: backend start failed: " << error << '\n';
    return kExitFailure;
  }

  bool stream_started = true;
  const auto started_at = std::chrono::system_clock::now();
  run_info.timestamps.started_at = started_at;

  auto stop_if_started = [&]() {
    if (!stream_started) {
      return;
    }

    std::string stop_error;
    (void)backend->Stop(stop_error);
    stream_started = false;
  };

  fs::path events_path;
  if (!AppendTraceEvent(
          events::EventType::kStreamStarted,
          started_at,
          {
              {"run_id", run_info.run_id},
              {"scenario_id", run_info.config.scenario_id},
              {"backend", run_info.config.backend},
              {"duration_ms", std::to_string(run_plan.duration.count())},
              {"fps", std::to_string(run_plan.sim_config.fps)},
              {"seed", std::to_string(run_plan.sim_config.seed)},
          },
          options.output_dir,
          events_path,
          error)) {
    stop_if_started();
    std::cerr << "error: failed to append STREAM_STARTED event: " << error << '\n';
    return kExitFailure;
  }

  const std::vector<backends::FrameSample> frames = backend->PullFrames(run_plan.duration, error);
  if (!error.empty()) {
    stop_if_started();
    std::cerr << "error: backend pull_frames failed: " << error << '\n';
    return kExitFailure;
  }

  std::uint64_t dropped_count = 0;
  std::uint64_t received_count = 0;
  std::optional<std::chrono::system_clock::time_point> latest_frame_ts;
  for (const auto& frame : frames) {
    const bool dropped = frame.dropped.has_value() && frame.dropped.value();
    if (!latest_frame_ts.has_value() || frame.timestamp > latest_frame_ts.value()) {
      latest_frame_ts = frame.timestamp;
    }

    std::map<std::string, std::string> payload = {
        {"run_id", run_info.run_id},
        {"frame_id", std::to_string(frame.frame_id)},
        {"size_bytes", std::to_string(frame.size_bytes)},
        {"dropped", dropped ? "true" : "false"},
    };

    if (dropped) {
      ++dropped_count;
      payload["reason"] = "sim_fault_injection";
    } else {
      ++received_count;
    }

    if (!AppendTraceEvent(dropped ? events::EventType::kFrameDropped : events::EventType::kFrameReceived,
                          frame.timestamp,
                          std::move(payload),
                          options.output_dir,
                          events_path,
                          error)) {
      stop_if_started();
      std::cerr << "error: failed to append frame event: " << error << '\n';
      return kExitFailure;
    }
  }

  if (!backend->Stop(error)) {
    std::cerr << "error: backend stop failed: " << error << '\n';
    return kExitFailure;
  }
  stream_started = false;

  auto finished_at = std::chrono::system_clock::now();
  if (latest_frame_ts.has_value() && finished_at < latest_frame_ts.value()) {
    finished_at = latest_frame_ts.value();
  }
  run_info.timestamps.finished_at = finished_at;

  if (!AppendTraceEvent(
          events::EventType::kStreamStopped,
          finished_at,
          {
              {"run_id", run_info.run_id},
              {"frames_total", std::to_string(frames.size())},
              {"frames_received", std::to_string(received_count)},
              {"frames_dropped", std::to_string(dropped_count)},
          },
          options.output_dir,
          events_path,
          error)) {
    std::cerr << "error: failed to append STREAM_STOPPED event: " << error << '\n';
    return kExitFailure;
  }

  fs::path run_artifact_path;
  if (!artifacts::WriteRunJson(run_info, options.output_dir, run_artifact_path, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  metrics::FpsReport fps_report;
  if (!metrics::ComputeFpsReport(frames,
                                 run_plan.duration,
                                 std::chrono::milliseconds(1'000),
                                 fps_report,
                                 error)) {
    std::cerr << "error: failed to compute fps metrics: " << error << '\n';
    return kExitFailure;
  }

  fs::path metrics_path;
  if (!metrics::WriteFpsMetricsCsv(fps_report, options.output_dir, metrics_path, error)) {
    std::cerr << "error: failed to write metrics.csv: " << error << '\n';
    return kExitFailure;
  }

  std::cout << "run queued: " << options.scenario_path << '\n';
  std::cout << "artifact: " << run_artifact_path.string() << '\n';
  std::cout << "events: " << events_path.string() << '\n';
  std::cout << "metrics: " << metrics_path.string() << '\n';
  std::cout << "fps: avg=" << fps_report.avg_fps
            << " rolling_samples=" << fps_report.rolling_samples.size() << '\n';
  std::cout << "frames: total=" << frames.size() << " received=" << received_count
            << " dropped=" << dropped_count << '\n';
  return kExitSuccess;
}

} // namespace

int Dispatch(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(std::cerr);
    return kExitUsage;
  }

  const std::string_view command(argv[1]);
  const std::vector<std::string_view> args(argv + 2, argv + argc);

  // Explicit command dispatch keeps behavior obvious and easy to evolve while
  // command count is still small.
  if (command == "version") {
    return CommandVersion(args);
  }

  if (command == "validate") {
    return CommandValidate(args);
  }

  if (command == "run") {
    return CommandRun(args);
  }

  if (command == "help" || command == "--help" || command == "-h") {
    PrintUsage(std::cout);
    return kExitSuccess;
  }

  std::cerr << "error: unknown subcommand: " << command << '\n';
  PrintUsage(std::cerr);
  return kExitUsage;
}

} // namespace labops::cli
