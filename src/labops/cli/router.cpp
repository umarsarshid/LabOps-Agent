#include "labops/cli/router.hpp"

#include "artifacts/bundle_manifest_writer.hpp"
#include "artifacts/metrics_writer.hpp"
#include "artifacts/metrics_diff_writer.hpp"
#include "artifacts/run_summary_writer.hpp"
#include "artifacts/run_writer.hpp"
#include "artifacts/scenario_writer.hpp"
#include "artifacts/bundle_zip_writer.hpp"
#include "backends/camera_backend.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "core/schema/run_contract.hpp"
#include "events/event_model.hpp"
#include "events/jsonl_writer.hpp"
#include "metrics/fps.hpp"
#include "scenarios/validator.hpp"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
      << "  labops run <scenario.json> [--out <dir>] [--zip]\n"
      << "  labops baseline capture <scenario.json>\n"
      << "  labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]\n"
      << "  labops validate <scenario.json>\n"
      << "  labops version\n";
}

// Keep nested baseline command help local so usage errors stay actionable.
void PrintBaselineUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops baseline capture <scenario.json>\n";
}

// Filesystem preflight checks run before schema validation. This keeps path and
// file-type failures separate from field-level schema issues.
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

  scenarios::ValidationReport report;
  if (!scenarios::ValidateScenarioFile(scenario_path, report, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  if (!report.valid) {
    std::cerr << "invalid scenario: " << scenario_path << '\n';
    for (const auto& issue : report.issues) {
      std::cerr << "  - " << issue.path << ": " << issue.message << '\n';
    }
    return kExitFailure;
  }

  std::cout << "valid: " << scenario_path << '\n';
  return kExitSuccess;
}

struct RunOptions {
  std::string scenario_path;
  fs::path output_dir = "out";
  bool zip_bundle = false;
};

struct RunPlan {
  backends::sim::SimScenarioConfig sim_config;
  std::chrono::milliseconds duration{1'000};
  struct Thresholds {
    std::optional<double> min_avg_fps;
    std::optional<double> max_drop_rate_percent;
    std::optional<double> max_inter_frame_interval_p95_us;
    std::optional<double> max_inter_frame_jitter_p95_us;
    std::optional<std::uint64_t> max_disconnect_count;
  } thresholds;
};

struct CompareOptions {
  fs::path baseline_path;
  fs::path run_path;
  fs::path output_dir;
  bool has_output_dir = false;
};

// Parse `run` args with an explicit contract:
// - one scenario path
// - optional `--out <dir>`
// Any unknown flags or duplicate positional args are treated as usage errors.
bool ParseRunOptions(const std::vector<std::string_view>& args, RunOptions& options,
                     std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--zip") {
      options.zip_bundle = true;
      continue;
    }
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

// Parse the milestone baseline contract:
// - exactly one scenario path
// - baseline target is deterministic: `baselines/<scenario_id>/`
bool ParseBaselineCaptureOptions(const std::vector<std::string_view>& args, RunOptions& options,
                                 std::string& error) {
  if (args.size() != 1U) {
    error = "baseline capture requires exactly 1 argument: <scenario.json>";
    return false;
  }

  options.scenario_path = std::string(args.front());
  const std::string scenario_id = fs::path(options.scenario_path).stem().string();
  if (scenario_id.empty()) {
    error = "unable to derive scenario_id from path: " + options.scenario_path;
    return false;
  }

  options.output_dir = fs::path("baselines") / scenario_id;
  options.zip_bundle = false;
  return true;
}

// Parse compare options with explicit long flags to keep invocation readable in
// CI and release verification scripts.
bool ParseCompareOptions(const std::vector<std::string_view>& args, CompareOptions& options,
                         std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--baseline") {
      if (i + 1 >= args.size()) {
        error = "missing value for --baseline";
        return false;
      }
      options.baseline_path = fs::path(args[i + 1]);
      ++i;
      continue;
    }

    if (token == "--run") {
      if (i + 1 >= args.size()) {
        error = "missing value for --run";
        return false;
      }
      options.run_path = fs::path(args[i + 1]);
      ++i;
      continue;
    }

    if (token == "--out") {
      if (i + 1 >= args.size()) {
        error = "missing value for --out";
        return false;
      }
      options.output_dir = fs::path(args[i + 1]);
      options.has_output_dir = true;
      ++i;
      continue;
    }

    error = "unknown option: " + std::string(token);
    return false;
  }

  if (options.baseline_path.empty()) {
    error = "compare requires --baseline <dir|metrics.csv>";
    return false;
  }
  if (options.run_path.empty()) {
    error = "compare requires --run <dir|metrics.csv>";
    return false;
  }
  if (!options.has_output_dir) {
    options.output_dir = options.run_path;
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

// Compare accepts either a bundle directory (containing metrics.csv) or a
// direct path to metrics.csv to make command usage flexible for operators.
bool ResolveMetricsCsvPath(const fs::path& input_path, fs::path& metrics_csv_path,
                           std::string& error) {
  if (input_path.empty()) {
    error = "metrics input path cannot be empty";
    return false;
  }

  std::error_code ec;
  if (!fs::exists(input_path, ec) || ec) {
    error = "path does not exist: " + input_path.string();
    return false;
  }

  if (fs::is_regular_file(input_path, ec) && !ec) {
    if (input_path.filename() != "metrics.csv") {
      error = "metrics file path must point to metrics.csv: " + input_path.string();
      return false;
    }
    metrics_csv_path = input_path;
    return true;
  }

  if (fs::is_directory(input_path, ec) && !ec) {
    const fs::path candidate = input_path / "metrics.csv";
    if (!fs::exists(candidate, ec) || ec || !fs::is_regular_file(candidate, ec)) {
      error = "metrics.csv not found in directory: " + input_path.string();
      return false;
    }
    metrics_csv_path = candidate;
    return true;
  }

  error = "path must be a directory or metrics.csv file: " + input_path.string();
  return false;
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

// Lightweight floating-point field extraction for threshold wiring.
// Supports JSON number forms:
// - 123
// - 123.45
// - 1.23e+3
std::optional<double> FindNumberJsonField(const std::string& text, std::string_view key) {
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
  if (value_pos < text.size() && (text[value_pos] == '+' || text[value_pos] == '-')) {
    ++value_pos;
  }

  bool has_digits = false;
  while (value_pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[value_pos])) != 0) {
    ++value_pos;
    has_digits = true;
  }

  if (value_pos < text.size() && text[value_pos] == '.') {
    ++value_pos;
    while (value_pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[value_pos])) != 0) {
      ++value_pos;
      has_digits = true;
    }
  }

  if (!has_digits) {
    return std::nullopt;
  }

  if (value_pos < text.size() && (text[value_pos] == 'e' || text[value_pos] == 'E')) {
    ++value_pos;
    if (value_pos < text.size() && (text[value_pos] == '+' || text[value_pos] == '-')) {
      ++value_pos;
    }

    std::size_t exponent_digits = 0;
    while (value_pos < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[value_pos])) != 0) {
      ++value_pos;
      ++exponent_digits;
    }
    if (exponent_digits == 0U) {
      return std::nullopt;
    }
  }

  const std::string value_text = text.substr(start, value_pos - start);
  char* parse_end = nullptr;
  const double parsed = std::strtod(value_text.c_str(), &parse_end);
  if (parse_end == nullptr || *parse_end != '\0') {
    return std::nullopt;
  }
  if (!std::isfinite(parsed)) {
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

  auto assign_non_negative_double = [&](std::string_view key, std::optional<double>& target,
                                        bool percent_0_to_100 = false) {
    const auto value = FindNumberJsonField(scenario_text, key);
    if (!value.has_value()) {
      return true;
    }

    const double parsed = value.value();
    if (!std::isfinite(parsed) || parsed < 0.0) {
      error = "scenario threshold must be a non-negative number for key: " + std::string(key);
      return false;
    }
    if (percent_0_to_100 && parsed > 100.0) {
      error = "scenario threshold must be in range [0,100] for key: " + std::string(key);
      return false;
    }
    target = parsed;
    return true;
  };

  auto assign_non_negative_integer_threshold = [&](std::string_view key, std::optional<std::uint64_t>& target) {
    const auto value = FindNumberJsonField(scenario_text, key);
    if (!value.has_value()) {
      return true;
    }

    const double parsed = value.value();
    if (!std::isfinite(parsed) || parsed < 0.0) {
      error = "scenario threshold must be a non-negative integer for key: " + std::string(key);
      return false;
    }
    const double floored = std::floor(parsed);
    if (floored != parsed ||
        floored > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
      error = "scenario threshold must be a non-negative integer for key: " + std::string(key);
      return false;
    }
    target = static_cast<std::uint64_t>(floored);
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

  if (!assign_non_negative_double("min_avg_fps", plan.thresholds.min_avg_fps)) {
    return false;
  }
  if (!assign_non_negative_double("max_drop_rate_percent",
                                  plan.thresholds.max_drop_rate_percent,
                                  /*percent_0_to_100=*/true)) {
    return false;
  }
  if (!assign_non_negative_double("max_inter_frame_interval_p95_us",
                                  plan.thresholds.max_inter_frame_interval_p95_us)) {
    return false;
  }
  if (!assign_non_negative_double("max_inter_frame_jitter_p95_us",
                                  plan.thresholds.max_inter_frame_jitter_p95_us)) {
    return false;
  }
  if (!assign_non_negative_integer_threshold("max_disconnect_count",
                                             plan.thresholds.max_disconnect_count)) {
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

// Bundle layout contract:
// - one subdirectory per run ID
// - all run artifacts emitted into that directory
//
// This keeps repeated runs under the same `--out` root isolated and shareable.
fs::path BuildRunBundleDir(const RunOptions& options, const core::schema::RunInfo& run_info) {
  return options.output_dir / run_info.run_id;
}

// Baseline capture reuses run execution but writes directly to a stable
// scenario-scoped directory (`baselines/<scenario_id>/`) instead of nesting by
// run ID.
fs::path ResolveExecutionOutputDir(const RunOptions& options, const core::schema::RunInfo& run_info,
                                   bool use_per_run_bundle_dir) {
  if (use_per_run_bundle_dir) {
    return BuildRunBundleDir(options, run_info);
  }
  return options.output_dir;
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

std::map<std::string, std::string>
BuildConfigAppliedPayload(const core::schema::RunInfo& run_info,
                          const backends::BackendConfig& applied_params) {
  std::map<std::string, std::string> payload = {
      {"run_id", run_info.run_id},
      {"scenario_id", run_info.config.scenario_id},
      {"applied_count", std::to_string(applied_params.size())},
  };

  // Prefix backend params so reserved metadata fields (`run_id`, `scenario_id`)
  // remain unambiguous in downstream parsers.
  for (const auto& [key, value] : applied_params) {
    payload["param." + key] = value;
  }

  return payload;
}

// Evaluates scenario pass/fail thresholds against computed metrics.
// Returns true when all configured thresholds pass and appends actionable
// failure reasons otherwise.
bool EvaluateRunThresholds(const RunPlan::Thresholds& thresholds,
                           const metrics::FpsReport& report,
                           std::vector<std::string>& failures) {
  failures.clear();

  auto check_min = [&](std::string_view label, double actual, const std::optional<double>& minimum) {
    if (!minimum.has_value()) {
      return;
    }
    if (actual + 1e-9 < minimum.value()) {
      failures.push_back(std::string(label) + " actual=" + std::to_string(actual) +
                         " is below minimum=" + std::to_string(minimum.value()));
    }
  };

  auto check_max = [&](std::string_view label, double actual, const std::optional<double>& maximum) {
    if (!maximum.has_value()) {
      return;
    }
    if (actual - 1e-9 > maximum.value()) {
      failures.push_back(std::string(label) + " actual=" + std::to_string(actual) +
                         " exceeds maximum=" + std::to_string(maximum.value()));
    }
  };

  check_min("avg_fps", report.avg_fps, thresholds.min_avg_fps);
  check_max("drop_rate_percent", report.drop_rate_percent, thresholds.max_drop_rate_percent);
  check_max("inter_frame_interval_p95_us",
            report.inter_frame_interval_us.p95_us,
            thresholds.max_inter_frame_interval_p95_us);
  check_max("inter_frame_jitter_p95_us",
            report.inter_frame_jitter_us.p95_us,
            thresholds.max_inter_frame_jitter_p95_us);

  if (thresholds.max_disconnect_count.has_value()) {
    constexpr std::uint64_t kObservedDisconnectCount = 0;
    if (kObservedDisconnectCount > thresholds.max_disconnect_count.value()) {
      failures.push_back("disconnect_count actual=" + std::to_string(kObservedDisconnectCount) +
                         " exceeds maximum=" +
                         std::to_string(thresholds.max_disconnect_count.value()));
    }
  }

  return failures.empty();
}

// Produces a short, human-readable anomaly list for run-level summary output.
// This keeps triage focused by highlighting the most actionable metric signals.
std::vector<std::string> BuildTopAnomalies(const metrics::FpsReport& report,
                                           std::uint32_t configured_fps,
                                           const std::vector<std::string>& threshold_failures) {
  std::vector<std::string> anomalies;

  for (const auto& failure : threshold_failures) {
    anomalies.push_back("Threshold violation: " + failure);
  }

  if (report.received_frames_total == 0U) {
    anomalies.push_back("No frames were received during the run.");
  }

  if (report.dropped_frames_total > 0U) {
    anomalies.push_back("Dropped " + std::to_string(report.dropped_frames_total) + " of " +
                        std::to_string(report.frames_total) + " frames (" +
                        std::to_string(report.drop_rate_percent) + "%).");
  }

  if (configured_fps > 0U) {
    const double expected_interval_us = 1'000'000.0 / static_cast<double>(configured_fps);
    const double avg_fps_floor = static_cast<double>(configured_fps) * 0.90;
    if (report.avg_fps + 1e-9 < avg_fps_floor) {
      anomalies.push_back("Average FPS " + std::to_string(report.avg_fps) +
                          " is below 90% of configured FPS " +
                          std::to_string(configured_fps) + ".");
    }

    if (report.inter_frame_interval_us.sample_count > 0U &&
        report.inter_frame_interval_us.p95_us > expected_interval_us * 1.50) {
      anomalies.push_back("Inter-frame interval p95 " +
                          std::to_string(report.inter_frame_interval_us.p95_us) +
                          "us is >150% of expected " +
                          std::to_string(expected_interval_us) + "us.");
    }

    if (report.inter_frame_jitter_us.sample_count > 0U &&
        report.inter_frame_jitter_us.p95_us > expected_interval_us * 0.50) {
      anomalies.push_back("Inter-frame jitter p95 " +
                          std::to_string(report.inter_frame_jitter_us.p95_us) +
                          "us is high relative to expected cadence " +
                          std::to_string(expected_interval_us) + "us.");
    }
  }

  if (anomalies.empty()) {
    anomalies.push_back("No notable anomalies detected by current heuristics.");
  }

  if (anomalies.size() > 3U) {
    anomalies.resize(3U);
  }

  return anomalies;
}

// Centralized run execution keeps `run` and `baseline capture` behavior aligned
// so artifact contracts and metrics math never diverge between modes.
int ExecuteScenarioRun(const RunOptions& options, bool use_per_run_bundle_dir,
                       bool allow_zip_bundle, std::string_view success_prefix) {
  std::string error;
  if (options.zip_bundle && !allow_zip_bundle) {
    std::cerr << "error: zip output is not supported for this command\n";
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
  const fs::path bundle_dir =
      ResolveExecutionOutputDir(options, run_info, use_per_run_bundle_dir);

  fs::path scenario_artifact_path;
  if (!artifacts::WriteScenarioJson(options.scenario_path, bundle_dir, scenario_artifact_path, error)) {
    std::cerr << "error: failed to write scenario snapshot: " << error << '\n';
    return kExitFailure;
  }

  std::unique_ptr<backends::ICameraBackend> backend =
      std::make_unique<backends::sim::SimCameraBackend>();

  if (!backend->Connect(error)) {
    std::cerr << "error: backend connect failed: " << error << '\n';
    return kExitFailure;
  }

  backends::BackendConfig applied_params;
  if (!backends::sim::ApplyScenarioConfig(*backend, run_plan.sim_config, error, &applied_params)) {
    std::cerr << "error: backend config failed: " << error << '\n';
    return kExitFailure;
  }

  fs::path events_path;
  const auto config_applied_at = std::chrono::system_clock::now();
  if (!AppendTraceEvent(events::EventType::kConfigApplied,
                        config_applied_at,
                        BuildConfigAppliedPayload(run_info, applied_params),
                        bundle_dir,
                        events_path,
                        error)) {
    std::cerr << "error: failed to append CONFIG_APPLIED event: " << error << '\n';
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
          bundle_dir,
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
                          bundle_dir,
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
          bundle_dir,
          events_path,
          error)) {
    std::cerr << "error: failed to append STREAM_STOPPED event: " << error << '\n';
    return kExitFailure;
  }

  fs::path run_artifact_path;
  if (!artifacts::WriteRunJson(run_info, bundle_dir, run_artifact_path, error)) {
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

  fs::path metrics_csv_path;
  if (!artifacts::WriteMetricsCsv(fps_report, bundle_dir, metrics_csv_path, error)) {
    std::cerr << "error: failed to write metrics.csv: " << error << '\n';
    return kExitFailure;
  }

  fs::path metrics_json_path;
  if (!artifacts::WriteMetricsJson(fps_report, bundle_dir, metrics_json_path, error)) {
    std::cerr << "error: failed to write metrics.json: " << error << '\n';
    return kExitFailure;
  }

  std::vector<std::string> threshold_failures;
  const bool thresholds_passed = EvaluateRunThresholds(run_plan.thresholds, fps_report, threshold_failures);
  const std::vector<std::string> top_anomalies =
      BuildTopAnomalies(fps_report, run_plan.sim_config.fps, threshold_failures);

  fs::path summary_markdown_path;
  if (!artifacts::WriteRunSummaryMarkdown(run_info,
                                          fps_report,
                                          run_plan.sim_config.fps,
                                          thresholds_passed,
                                          threshold_failures,
                                          top_anomalies,
                                          bundle_dir,
                                          summary_markdown_path,
                                          error)) {
    std::cerr << "error: failed to write summary.md: " << error << '\n';
    return kExitFailure;
  }

  fs::path bundle_manifest_path;
  const std::vector<fs::path> bundle_artifact_paths = {
      scenario_artifact_path,
      run_artifact_path,
      events_path,
      metrics_csv_path,
      metrics_json_path,
      summary_markdown_path,
  };
  if (!artifacts::WriteBundleManifestJson(bundle_dir, bundle_artifact_paths, bundle_manifest_path, error)) {
    std::cerr << "error: failed to write bundle manifest: " << error << '\n';
    return kExitFailure;
  }

  fs::path bundle_zip_path;
  if (options.zip_bundle) {
    if (!artifacts::WriteBundleZip(bundle_dir, bundle_zip_path, error)) {
      std::cerr << "error: failed to write support bundle zip: " << error << '\n';
      return kExitFailure;
    }
  }

  std::cout << success_prefix << options.scenario_path << '\n';
  std::cout << "bundle: " << bundle_dir.string() << '\n';
  std::cout << "scenario: " << scenario_artifact_path.string() << '\n';
  std::cout << "artifact: " << run_artifact_path.string() << '\n';
  std::cout << "events: " << events_path.string() << '\n';
  std::cout << "metrics_csv: " << metrics_csv_path.string() << '\n';
  std::cout << "metrics_json: " << metrics_json_path.string() << '\n';
  std::cout << "summary: " << summary_markdown_path.string() << '\n';
  std::cout << "manifest: " << bundle_manifest_path.string() << '\n';
  if (options.zip_bundle) {
    std::cout << "bundle_zip: " << bundle_zip_path.string() << '\n';
  }
  std::cout << "fps: avg=" << fps_report.avg_fps
            << " rolling_samples=" << fps_report.rolling_samples.size() << '\n';
  std::cout << "drops: total=" << fps_report.dropped_frames_total
            << " rate_percent=" << fps_report.drop_rate_percent << '\n';
  std::cout << "timing_us: interval_avg=" << fps_report.inter_frame_interval_us.avg_us
            << " interval_p95=" << fps_report.inter_frame_interval_us.p95_us
            << " jitter_avg=" << fps_report.inter_frame_jitter_us.avg_us
            << " jitter_p95=" << fps_report.inter_frame_jitter_us.p95_us << '\n';
  std::cout << "frames: total=" << frames.size() << " received=" << received_count
            << " dropped=" << dropped_count << '\n';
  if (thresholds_passed) {
    std::cout << "thresholds: pass\n";
    return kExitSuccess;
  }

  std::cout << "thresholds: fail count=" << threshold_failures.size() << '\n';
  for (const auto& failure : threshold_failures) {
    std::cerr << "threshold failed: " << failure << '\n';
  }
  return kExitFailure;
}

int CommandRun(const std::vector<std::string_view>& args) {
  RunOptions options;
  std::string error;
  if (!ParseRunOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  return ExecuteScenarioRun(options,
                            /*use_per_run_bundle_dir=*/true,
                            /*allow_zip_bundle=*/true,
                            "run queued: ");
}

int CommandBaselineCapture(const std::vector<std::string_view>& args) {
  RunOptions options;
  std::string error;
  if (!ParseBaselineCaptureOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  return ExecuteScenarioRun(options,
                            /*use_per_run_bundle_dir=*/false,
                            /*allow_zip_bundle=*/false,
                            "baseline captured: ");
}

int CommandBaseline(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "error: baseline requires a subcommand\n";
    PrintBaselineUsage(std::cerr);
    return kExitUsage;
  }

  const std::string_view subcommand = args.front();
  const std::vector<std::string_view> sub_args(args.begin() + 1, args.end());
  if (subcommand == "capture") {
    return CommandBaselineCapture(sub_args);
  }

  std::cerr << "error: unknown baseline subcommand: " << subcommand << '\n';
  PrintBaselineUsage(std::cerr);
  return kExitUsage;
}

int CommandCompare(const std::vector<std::string_view>& args) {
  CompareOptions options;
  std::string error;
  if (!ParseCompareOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  fs::path baseline_metrics_csv_path;
  if (!ResolveMetricsCsvPath(options.baseline_path, baseline_metrics_csv_path, error)) {
    std::cerr << "error: failed to resolve baseline metrics: " << error << '\n';
    return kExitFailure;
  }

  fs::path run_metrics_csv_path;
  if (!ResolveMetricsCsvPath(options.run_path, run_metrics_csv_path, error)) {
    std::cerr << "error: failed to resolve run metrics: " << error << '\n';
    return kExitFailure;
  }

  fs::path output_dir = options.output_dir;
  if (!options.has_output_dir) {
    std::error_code ec;
    if (fs::is_regular_file(options.run_path, ec) && !ec) {
      output_dir = options.run_path.parent_path();
      if (output_dir.empty()) {
        output_dir = ".";
      }
    }
  }

  artifacts::MetricsDiffReport diff_report;
  if (!artifacts::ComputeMetricsDiffFromCsv(baseline_metrics_csv_path,
                                            run_metrics_csv_path,
                                            diff_report,
                                            error)) {
    std::cerr << "error: failed to compare metrics: " << error << '\n';
    return kExitFailure;
  }

  fs::path diff_json_path;
  if (!artifacts::WriteMetricsDiffJson(diff_report, output_dir, diff_json_path, error)) {
    std::cerr << "error: failed to write diff.json: " << error << '\n';
    return kExitFailure;
  }

  fs::path diff_markdown_path;
  if (!artifacts::WriteMetricsDiffMarkdown(diff_report, output_dir, diff_markdown_path, error)) {
    std::cerr << "error: failed to write diff.md: " << error << '\n';
    return kExitFailure;
  }

  std::cout << "compare baseline: " << baseline_metrics_csv_path.string() << '\n';
  std::cout << "compare run: " << run_metrics_csv_path.string() << '\n';
  std::cout << "diff_json: " << diff_json_path.string() << '\n';
  std::cout << "diff_md: " << diff_markdown_path.string() << '\n';
  std::cout << "compared_metrics: " << diff_report.deltas.size() << '\n';
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

  if (command == "baseline") {
    return CommandBaseline(args);
  }

  if (command == "compare") {
    return CommandCompare(args);
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
