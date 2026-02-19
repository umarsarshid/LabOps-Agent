#include "labops/cli/router.hpp"

#include "artifacts/bundle_manifest_writer.hpp"
#include "artifacts/bundle_registry.hpp"
#include "artifacts/bundle_zip_writer.hpp"
#include "artifacts/camera_config_writer.hpp"
#include "artifacts/config_report_writer.hpp"
#include "artifacts/config_verify_writer.hpp"
#include "artifacts/hostprobe_writer.hpp"
#include "artifacts/html_report_writer.hpp"
#include "artifacts/kb_draft_writer.hpp"
#include "artifacts/metrics_diff_writer.hpp"
#include "artifacts/metrics_writer.hpp"
#include "artifacts/run_summary_writer.hpp"
#include "artifacts/run_writer.hpp"
#include "artifacts/scenario_writer.hpp"
#include "backends/camera_backend.hpp"
#include "backends/real_sdk/apply_params.hpp"
#include "backends/real_sdk/error_mapper.hpp"
#include "backends/real_sdk/real_backend_factory.hpp"
#include "backends/real_sdk/reconnect_policy.hpp"
#include "backends/real_sdk/transport_counters.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "backends/webcam/device_selector.hpp"
#include "backends/webcam/webcam_factory.hpp"
#include "core/errors/exit_codes.hpp"
#include "core/json_dom.hpp"
#include "core/schema/run_contract.hpp"
#include "events/emitter.hpp"
#include "events/event_model.hpp"
#include "events/transport_anomaly.hpp"
#include "hostprobe/system_probe.hpp"
#include "labops/soak/checkpoint_store.hpp"
#include "metrics/anomalies.hpp"
#include "metrics/fps.hpp"
#include "scenarios/model.hpp"
#include "scenarios/netem_profile_support.hpp"
#include "scenarios/validator.hpp"

#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace labops::cli {

namespace {

constexpr std::string_view kBackendSim = "sim";
constexpr std::string_view kBackendWebcam = "webcam";
constexpr std::string_view kBackendRealStub = "real_stub";

using JsonValue = core::json::Value;
using JsonParser = core::json::Parser;

// Keep local names for readability while using one shared core contract.
constexpr int kExitSuccess = core::errors::ToInt(core::errors::ExitCode::kSuccess);
constexpr int kExitFailure = core::errors::ToInt(core::errors::ExitCode::kFailure);
constexpr int kExitUsage = core::errors::ToInt(core::errors::ExitCode::kUsage);
constexpr int kExitSchemaInvalid = core::errors::ToInt(core::errors::ExitCode::kSchemaInvalid);
constexpr int kExitBackendConnectFailed =
    core::errors::ToInt(core::errors::ExitCode::kBackendConnectFailed);
constexpr int kExitThresholdsFailed =
    core::errors::ToInt(core::errors::ExitCode::kThresholdsFailed);

// One usage text source avoids divergence between help and error paths.
void PrintUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops run <scenario.json> [--out <dir>] [--zip] [--redact] "
         "[--device <selector>] [--sdk-log] "
         "[--soak] [--checkpoint-interval-ms <ms>] [--resume <checkpoint.json>] "
         "[--soak-stop-file <path>] "
         "[--log-level <debug|info|warn|error>] "
         "[--apply-netem --netem-iface <iface> [--apply-netem-force]]\n"
      << "  labops baseline capture <scenario.json> [--redact] "
         "[--device <selector>] [--sdk-log] "
         "[--log-level <debug|info|warn|error>] "
         "[--apply-netem --netem-iface <iface> [--apply-netem-force]]\n"
      << "  labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]\n"
      << "  labops kb draft --run <run_folder> [--out <kb_draft.md>]\n"
      << "  labops list-backends\n"
      << "  labops list-devices --backend <real>\n"
      << "  labops validate <scenario.json>\n"
      << "  labops version\n";
}

// Keep nested baseline command help local so usage errors stay actionable.
void PrintBaselineUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops baseline capture <scenario.json> [--redact] "
         "[--device <selector>] [--sdk-log] "
         "[--log-level <debug|info|warn|error>] "
         "[--apply-netem --netem-iface <iface> [--apply-netem-force]]\n";
}

void PrintKbUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops kb draft --run <run_folder> [--out <kb_draft.md>]\n";
}

void PrintListDevicesUsage(std::ostream& out) {
  out << "usage:\n"
      << "  labops list-devices --backend <real>\n";
}

// SIGINT is handled as a cooperative stop request for active runs.
// The handler only flips this atomic flag; run logic observes it at safe
// boundaries so we can flush artifacts instead of exiting mid-write.
std::atomic<bool> g_run_interrupt_requested{false};

void HandleInterruptSignal(int /*signal_number*/) {
  g_run_interrupt_requested.store(true);
}

bool ParsePositiveUInt64Arg(std::string_view text, std::uint64_t& value) {
  if (text.empty()) {
    return false;
  }
  std::uint64_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end || parsed == 0U) {
    return false;
  }
  value = parsed;
  return true;
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

int CommandListBackends(const std::vector<std::string_view>& args) {
  if (!args.empty()) {
    std::cerr << "error: list-backends does not accept arguments\n";
    return kExitUsage;
  }

  std::cout << "sim ✅ enabled\n";
  const backends::webcam::WebcamBackendAvailability webcam_availability =
      backends::webcam::GetWebcamBackendAvailability();
  if (webcam_availability.available) {
    std::cout << "webcam ✅ enabled\n";
  } else {
    std::cout << "webcam ⚠️ disabled (" << webcam_availability.reason << ")\n";
  }
  if (backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    std::cout << "real ✅ enabled\n";
  } else {
    std::cout << "real ⚠️ " << backends::real_sdk::RealBackendAvailabilityStatusText() << '\n';
  }
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
    return kExitSchemaInvalid;
  }

  std::cout << "valid: " << scenario_path << '\n';
  return kExitSuccess;
}

struct RunPlan {
  backends::sim::SimScenarioConfig sim_config;
  std::chrono::milliseconds duration{1'000};
  std::string backend = std::string(kBackendSim);
  backends::real_sdk::ParamApplyMode real_apply_mode = backends::real_sdk::ParamApplyMode::kStrict;
  std::vector<backends::real_sdk::ApplyParamInput> real_params;
  std::optional<std::string> netem_profile;
  std::optional<std::string> device_selector;
  std::optional<backends::webcam::WebcamDeviceSelector> webcam_device_selector;
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

struct KbDraftOptions {
  fs::path run_folder;
  fs::path output_path;
  bool has_output_path = false;
};

struct ListDevicesOptions {
  std::string backend;
};

bool ValidateDeviceSelectorText(std::string_view selector_text, std::string& error);

// Parse `run` args with an explicit contract:
// - one scenario path
// - optional `--out <dir>`
// Any unknown flags or duplicate positional args are treated as usage errors.
bool ParseRunOptions(const std::vector<std::string_view>& args, RunOptions& options,
                     std::string& error) {
  bool checkpoint_interval_set = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--soak") {
      options.soak_mode = true;
      continue;
    }
    if (token == "--checkpoint-interval-ms") {
      if (i + 1 >= args.size()) {
        error = "missing value for --checkpoint-interval-ms";
        return false;
      }

      std::uint64_t parsed = 0;
      if (!ParsePositiveUInt64Arg(args[i + 1], parsed)) {
        error = "checkpoint interval must be a positive integer milliseconds value";
        return false;
      }
      if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error = "checkpoint interval is out of range";
        return false;
      }

      options.checkpoint_interval = std::chrono::milliseconds(static_cast<std::int64_t>(parsed));
      checkpoint_interval_set = true;
      ++i;
      continue;
    }
    if (token == "--resume") {
      if (i + 1 >= args.size()) {
        error = "missing value for --resume";
        return false;
      }
      options.resume_checkpoint_path = fs::path(args[i + 1]);
      options.soak_mode = true;
      ++i;
      continue;
    }
    if (token == "--soak-stop-file") {
      if (i + 1 >= args.size()) {
        error = "missing value for --soak-stop-file";
        return false;
      }
      options.soak_stop_file = fs::path(args[i + 1]);
      ++i;
      continue;
    }
    if (token == "--zip") {
      options.zip_bundle = true;
      continue;
    }
    if (token == "--redact") {
      options.redact_identifiers = true;
      continue;
    }
    if (token == "--sdk-log") {
      options.capture_sdk_log = true;
      continue;
    }
    if (token == "--apply-netem") {
      options.apply_netem = true;
      continue;
    }
    if (token == "--apply-netem-force") {
      options.apply_netem = true;
      options.apply_netem_force = true;
      continue;
    }
    if (token == "--netem-iface") {
      if (i + 1 >= args.size()) {
        error = "missing value for --netem-iface";
        return false;
      }
      options.netem_interface = std::string(args[i + 1]);
      ++i;
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
    if (token == "--log-level") {
      if (i + 1 >= args.size()) {
        error = "missing value for --log-level";
        return false;
      }
      core::logging::LogLevel parsed = core::logging::LogLevel::kInfo;
      if (!core::logging::ParseLogLevel(args[i + 1], parsed, error)) {
        return false;
      }
      options.log_level = parsed;
      ++i;
      continue;
    }
    if (token == "--device") {
      if (i + 1 >= args.size()) {
        error = "missing value for --device";
        return false;
      }
      if (!options.device_selector.empty()) {
        error = "--device may be provided at most once";
        return false;
      }
      options.device_selector = std::string(args[i + 1]);
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
  if (!options.soak_mode) {
    if (checkpoint_interval_set) {
      error = "--checkpoint-interval-ms requires --soak";
      return false;
    }
    if (!options.resume_checkpoint_path.empty()) {
      error = "--resume requires --soak";
      return false;
    }
    if (!options.soak_stop_file.empty()) {
      error = "--soak-stop-file requires --soak";
      return false;
    }
  }
  if (options.apply_netem && options.netem_interface.empty()) {
    error = "--apply-netem requires --netem-iface <iface>";
    return false;
  }
  if (!options.apply_netem && !options.netem_interface.empty()) {
    error = "--netem-iface requires --apply-netem";
    return false;
  }
  if (!options.device_selector.empty() &&
      !ValidateDeviceSelectorText(options.device_selector, error)) {
    return false;
  }

  return true;
}

// Parse the milestone baseline contract:
// - exactly one scenario path
// - baseline target is deterministic: `baselines/<scenario_id>/`
bool ParseBaselineCaptureOptions(const std::vector<std::string_view>& args, RunOptions& options,
                                 std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--redact") {
      options.redact_identifiers = true;
      continue;
    }
    if (token == "--sdk-log") {
      options.capture_sdk_log = true;
      continue;
    }
    if (token == "--apply-netem") {
      options.apply_netem = true;
      continue;
    }
    if (token == "--apply-netem-force") {
      options.apply_netem = true;
      options.apply_netem_force = true;
      continue;
    }
    if (token == "--netem-iface") {
      if (i + 1 >= args.size()) {
        error = "missing value for --netem-iface";
        return false;
      }
      options.netem_interface = std::string(args[i + 1]);
      ++i;
      continue;
    }
    if (token == "--log-level") {
      if (i + 1 >= args.size()) {
        error = "missing value for --log-level";
        return false;
      }
      core::logging::LogLevel parsed = core::logging::LogLevel::kInfo;
      if (!core::logging::ParseLogLevel(args[i + 1], parsed, error)) {
        return false;
      }
      options.log_level = parsed;
      ++i;
      continue;
    }
    if (token == "--device") {
      if (i + 1 >= args.size()) {
        error = "missing value for --device";
        return false;
      }
      if (!options.device_selector.empty()) {
        error = "--device may be provided at most once";
        return false;
      }
      options.device_selector = std::string(args[i + 1]);
      ++i;
      continue;
    }

    if (!token.empty() && token.front() == '-') {
      error = "unknown option: " + std::string(token);
      return false;
    }

    if (!options.scenario_path.empty()) {
      error = "baseline capture requires exactly 1 argument: <scenario.json>";
      return false;
    }
    options.scenario_path = std::string(token);
  }

  if (options.scenario_path.empty()) {
    error = "baseline capture requires exactly 1 argument: <scenario.json>";
    return false;
  }
  if (options.apply_netem && options.netem_interface.empty()) {
    error = "--apply-netem requires --netem-iface <iface>";
    return false;
  }
  if (!options.apply_netem && !options.netem_interface.empty()) {
    error = "--netem-iface requires --apply-netem";
    return false;
  }
  if (!options.device_selector.empty() &&
      !ValidateDeviceSelectorText(options.device_selector, error)) {
    return false;
  }

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

bool ParseKbDraftOptions(const std::vector<std::string_view>& args, KbDraftOptions& options,
                         std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--run") {
      if (i + 1 >= args.size()) {
        error = "missing value for --run";
        return false;
      }
      options.run_folder = fs::path(args[i + 1]);
      ++i;
      continue;
    }
    if (token == "--out") {
      if (i + 1 >= args.size()) {
        error = "missing value for --out";
        return false;
      }
      options.output_path = fs::path(args[i + 1]);
      options.has_output_path = true;
      ++i;
      continue;
    }

    error = "unknown option: " + std::string(token);
    return false;
  }

  if (options.run_folder.empty()) {
    error = "kb draft requires --run <run_folder>";
    return false;
  }
  if (!options.has_output_path) {
    options.output_path = options.run_folder / "kb_draft.md";
  }

  return true;
}

bool ParseListDevicesOptions(const std::vector<std::string_view>& args, ListDevicesOptions& options,
                             std::string& error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view token = args[i];
    if (token == "--backend") {
      if (i + 1 >= args.size()) {
        error = "missing value for --backend";
        return false;
      }
      options.backend = std::string(args[i + 1]);
      ++i;
      continue;
    }

    error = "unknown option: " + std::string(token);
    return false;
  }

  if (options.backend.empty()) {
    error = "list-devices requires --backend <real>";
    return false;
  }
  if (options.backend != "real") {
    error = "list-devices currently supports only --backend real";
    return false;
  }

  return true;
}

bool ValidateDeviceSelectorText(std::string_view selector_text, std::string& error) {
  backends::real_sdk::DeviceSelector real_selector;
  if (backends::real_sdk::ParseDeviceSelector(selector_text, real_selector, error)) {
    return true;
  }

  std::string real_error = error;
  backends::webcam::WebcamDeviceSelector webcam_selector;
  if (backends::webcam::ParseWebcamDeviceSelector(selector_text, webcam_selector, error)) {
    return true;
  }

  error = "invalid device selector '" + std::string(selector_text) +
          "': expected real selector (serial/user_id/index) or webcam selector "
          "(id/index/name_contains). real parser: " +
          real_error + "; webcam parser: " + error;
  return false;
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

const JsonValue* FindObjectMember(const JsonValue& object_value, std::string_view key) {
  if (object_value.type != JsonValue::Type::kObject) {
    return nullptr;
  }
  const auto it = object_value.object_value.find(std::string(key));
  if (it == object_value.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

const JsonValue* FindJsonPath(const JsonValue& root, std::initializer_list<std::string_view> path) {
  const JsonValue* cursor = &root;
  for (const std::string_view key : path) {
    cursor = FindObjectMember(*cursor, key);
    if (cursor == nullptr) {
      return nullptr;
    }
  }
  return cursor;
}

// Scenario field lookup with canonical+legacy fallback.
// Used where runtime parsing still supports historical flat fixture keys.
const JsonValue* FindScenarioField(const JsonValue& root,
                                   std::initializer_list<std::string_view> canonical_path,
                                   std::initializer_list<std::string_view> legacy_path = {}) {
  if (const JsonValue* value = FindJsonPath(root, canonical_path); value != nullptr) {
    return value;
  }
  if (legacy_path.size() == 0U) {
    return nullptr;
  }
  return FindJsonPath(root, legacy_path);
}

bool TryGetFiniteNumber(const JsonValue& value, double& out) {
  if (value.type != JsonValue::Type::kNumber || !std::isfinite(value.number_value)) {
    return false;
  }
  out = value.number_value;
  return true;
}

std::string FormatCompactDouble(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  std::string text = out.str();
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  if (text.empty()) {
    return "0";
  }
  return text;
}

void UpsertRealParam(std::vector<backends::real_sdk::ApplyParamInput>& params, std::string key,
                     std::string value) {
  for (auto& existing : params) {
    if (existing.generic_key == key) {
      existing.requested_value = std::move(value);
      return;
    }
  }
  params.push_back(backends::real_sdk::ApplyParamInput{.generic_key = std::move(key),
                                                       .requested_value = std::move(value)});
}

std::string FormatShellDouble(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << value;
  return out.str();
}

struct NetemProfileDefinition {
  double delay_ms = 0.0;
  double jitter_ms = 0.0;
  double loss_percent = 0.0;
  double reorder_percent = 0.0;
  double correlation_percent = 0.0;
};

bool LoadNetemProfileDefinition(const fs::path& profile_path, NetemProfileDefinition& definition,
                                std::string& error) {
  definition = NetemProfileDefinition{};
  std::string profile_text;
  if (!ReadTextFile(profile_path.string(), profile_text, error)) {
    return false;
  }

  JsonValue profile_root;
  JsonParser parser(profile_text);
  std::string parse_error;
  if (!parser.Parse(profile_root, parse_error)) {
    error = "invalid netem profile JSON: " + parse_error;
    return false;
  }
  if (profile_root.type != JsonValue::Type::kObject) {
    error = "netem profile root must be a JSON object";
    return false;
  }

  auto read_non_negative_number = [&](std::string_view key, double& target) {
    const JsonValue* value = FindScenarioField(profile_root, {"netem", key}, {key});
    if (value == nullptr) {
      return true;
    }
    double parsed = 0.0;
    if (!TryGetFiniteNumber(*value, parsed) || parsed < 0.0) {
      error = "netem profile field must be a non-negative number for key: " + std::string(key);
      return false;
    }
    target = parsed;
    return true;
  };

  if (!read_non_negative_number("delay_ms", definition.delay_ms) ||
      !read_non_negative_number("jitter_ms", definition.jitter_ms) ||
      !read_non_negative_number("loss_percent", definition.loss_percent) ||
      !read_non_negative_number("reorder_percent", definition.reorder_percent) ||
      !read_non_negative_number("correlation_percent", definition.correlation_percent)) {
    return false;
  }

  return true;
}

bool BuildNetemCommandSuggestions(const std::string& scenario_path, const RunPlan& run_plan,
                                  std::optional<artifacts::NetemCommandSuggestions>& suggestions,
                                  std::string& warning) {
  suggestions.reset();
  warning.clear();
  if (!run_plan.netem_profile.has_value()) {
    return true;
  }

  fs::path profile_path;
  if (!scenarios::ResolveNetemProfilePath(fs::path(scenario_path), run_plan.netem_profile.value(),
                                          profile_path)) {
    warning = "netem profile '" + run_plan.netem_profile.value() +
              "' was referenced but no profile file was found under tools/netem_profiles";
    return true;
  }

  NetemProfileDefinition definition;
  std::string error;
  if (!LoadNetemProfileDefinition(profile_path, definition, error)) {
    warning = "unable to load netem profile '" + run_plan.netem_profile.value() + "': " + error;
    return true;
  }

  artifacts::NetemCommandSuggestions netem;
  netem.profile_id = run_plan.netem_profile.value();
  netem.profile_path = profile_path;
  netem.apply_command = "sudo tc qdisc replace dev <iface> root netem delay " +
                        FormatShellDouble(definition.delay_ms) + "ms " +
                        FormatShellDouble(definition.jitter_ms) + "ms loss " +
                        FormatShellDouble(definition.loss_percent) + "% reorder " +
                        FormatShellDouble(definition.reorder_percent) + "% " +
                        FormatShellDouble(definition.correlation_percent) + "%";
  netem.show_command = "tc qdisc show dev <iface>";
  netem.teardown_command = "sudo tc qdisc del dev <iface> root";
  netem.safety_note = "Run manually on Linux and replace <iface> with your test NIC.";
  suggestions = std::move(netem);
  return true;
}

bool IsSafeNetemInterfaceName(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  for (const char c : name) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

std::string ReplaceIfacePlaceholder(std::string template_command, std::string_view iface) {
  const std::string needle = "<iface>";
  std::size_t pos = 0;
  while ((pos = template_command.find(needle, pos)) != std::string::npos) {
    template_command.replace(pos, needle.size(), iface);
    pos += iface.size();
  }
  return template_command;
}

bool RunShellCommandNoCapture(const std::string& command, int& exit_code, std::string& error) {
  error.clear();
  exit_code = -1;
  const int raw_status = std::system(command.c_str());
  if (raw_status == -1) {
    error = "failed to execute shell command";
    return false;
  }

#if defined(_WIN32)
  exit_code = raw_status;
#else
  if (WIFEXITED(raw_status)) {
    exit_code = WEXITSTATUS(raw_status);
  } else {
    exit_code = raw_status;
  }
#endif
  return true;
}

bool IsLinuxHost() {
#if defined(__linux__)
  return true;
#else
  return false;
#endif
}

bool IsRunningAsRoot() {
#if defined(__linux__) || defined(__APPLE__)
  return geteuid() == 0;
#else
  return false;
#endif
}

class ScopedNetemTeardown {
public:
  explicit ScopedNetemTeardown(core::logging::Logger* logger = nullptr) : logger_(logger) {}

  ~ScopedNetemTeardown() {
    if (!armed_) {
      return;
    }

    int exit_code = -1;
    std::string error;
    if (!RunShellCommandNoCapture(teardown_command_, exit_code, error)) {
      if (logger_ != nullptr) {
        logger_->Warn("netem teardown command execution failed",
                      {{"teardown_command", teardown_command_}, {"error", error}});
      } else {
        std::cerr << "warning: netem teardown failed to execute: " << error << '\n';
      }
      return;
    }
    if (exit_code != 0) {
      if (logger_ != nullptr) {
        logger_->Warn(
            "netem teardown returned non-zero exit code",
            {{"teardown_command", teardown_command_}, {"exit_code", std::to_string(exit_code)}});
      } else {
        std::cerr << "warning: netem teardown returned non-zero exit code: " << exit_code << '\n';
      }
    }
  }

  void Arm(std::string teardown_command) {
    teardown_command_ = std::move(teardown_command);
    armed_ = true;
  }

  ScopedNetemTeardown(const ScopedNetemTeardown&) = delete;
  ScopedNetemTeardown& operator=(const ScopedNetemTeardown&) = delete;

private:
  bool armed_ = false;
  std::string teardown_command_;
  core::logging::Logger* logger_ = nullptr;
};

bool ApplyNetemIfRequested(const RunOptions& options,
                           const std::optional<artifacts::NetemCommandSuggestions>& suggestions,
                           ScopedNetemTeardown& teardown_guard, std::string& error) {
  error.clear();
  if (!options.apply_netem) {
    return true;
  }
  if (!suggestions.has_value()) {
    error = "--apply-netem requires a valid scenario netem_profile";
    return false;
  }
  if (!IsLinuxHost()) {
    error = "--apply-netem is only supported on Linux hosts";
    return false;
  }
  if (!IsSafeNetemInterfaceName(options.netem_interface)) {
    error = "netem interface contains unsupported characters: " + options.netem_interface;
    return false;
  }
  if (!options.apply_netem_force && !IsRunningAsRoot()) {
    error = "--apply-netem requires root (run as root or use --apply-netem-force)";
    return false;
  }

  const std::string apply_command =
      ReplaceIfacePlaceholder(suggestions->apply_command, options.netem_interface);
  const std::string teardown_command =
      ReplaceIfacePlaceholder(suggestions->teardown_command, options.netem_interface);

  int apply_exit_code = -1;
  if (!RunShellCommandNoCapture(apply_command, apply_exit_code, error)) {
    error = "netem apply command failed: " + error;
    return false;
  }
  if (apply_exit_code != 0) {
    error = "netem apply command returned non-zero exit code: " + std::to_string(apply_exit_code);
    return false;
  }

  teardown_guard.Arm(teardown_command);
  return true;
}

bool LoadRunPlanFromScenario(const std::string& scenario_path, RunPlan& plan, std::string& error) {
  plan = RunPlan{};
  scenarios::ScenarioModel scenario_model;
  if (!scenarios::LoadScenarioModelFile(scenario_path, scenario_model, error)) {
    return false;
  }

  auto assign_u32 = [&](std::string_view key, const std::optional<std::uint64_t>& value,
                        std::uint32_t& target,
                        std::uint32_t max_value = std::numeric_limits<std::uint32_t>::max()) {
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

  auto assign_u64 = [&](const std::optional<std::uint64_t>& value, std::uint64_t& target) {
    if (!value.has_value()) {
      return true;
    }
    target = value.value();
    return true;
  };

  auto assign_non_negative_double = [&](std::string_view key, const std::optional<double>& value,
                                        std::optional<double>& target,
                                        bool percent_0_to_100 = false) {
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

  auto assign_non_negative_integer_threshold = [&](std::string_view key,
                                                   const std::optional<double>& value,
                                                   std::optional<std::uint64_t>& target) {
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

  if (scenario_model.duration.duration_ms.has_value()) {
    if (scenario_model.duration.duration_ms.value() == 0U) {
      error = "scenario duration_ms must be greater than 0";
      return false;
    }
    plan.duration = std::chrono::milliseconds(
        static_cast<std::int64_t>(scenario_model.duration.duration_ms.value()));
  } else if (scenario_model.duration.duration_s.has_value()) {
    if (scenario_model.duration.duration_s.value() == 0U) {
      error = "scenario duration_s must be greater than 0";
      return false;
    }
    plan.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(
        static_cast<std::int64_t>(scenario_model.duration.duration_s.value())));
  }

  if (scenario_model.backend.has_value()) {
    if (scenario_model.backend.value() != kBackendSim &&
        scenario_model.backend.value() != kBackendWebcam &&
        scenario_model.backend.value() != kBackendRealStub) {
      error = "scenario backend must be one of: sim, webcam, real_stub";
      return false;
    }
    plan.backend = scenario_model.backend.value();
  }

  if (scenario_model.apply_mode.has_value()) {
    if (!backends::real_sdk::ParseParamApplyMode(scenario_model.apply_mode.value(),
                                                 plan.real_apply_mode, error)) {
      return false;
    }
  }

  if (!assign_u32("fps", scenario_model.camera.fps, plan.sim_config.fps)) {
    return false;
  }
  if (!assign_u32("jitter_us", scenario_model.sim_faults.jitter_us, plan.sim_config.jitter_us)) {
    return false;
  }
  if (!assign_u64(scenario_model.sim_faults.seed, plan.sim_config.seed)) {
    return false;
  }
  if (!assign_u32("frame_size_bytes", scenario_model.camera.frame_size_bytes,
                  plan.sim_config.frame_size_bytes)) {
    return false;
  }
  if (!assign_u32("drop_every_n", scenario_model.sim_faults.drop_every_n,
                  plan.sim_config.drop_every_n)) {
    return false;
  }
  if (!assign_u32("drop_percent", scenario_model.sim_faults.drop_percent,
                  plan.sim_config.faults.drop_percent, 100U)) {
    return false;
  }
  if (!assign_u32("burst_drop", scenario_model.sim_faults.burst_drop,
                  plan.sim_config.faults.burst_drop)) {
    return false;
  }
  if (!assign_u32("reorder", scenario_model.sim_faults.reorder, plan.sim_config.faults.reorder)) {
    return false;
  }

  if (scenario_model.camera.fps.has_value()) {
    UpsertRealParam(plan.real_params, "frame_rate", std::to_string(plan.sim_config.fps));
  }
  if (scenario_model.camera.pixel_format.has_value() &&
      !scenario_model.camera.pixel_format->empty()) {
    UpsertRealParam(plan.real_params, "pixel_format", scenario_model.camera.pixel_format.value());
  }
  if (scenario_model.camera.exposure_us.has_value()) {
    UpsertRealParam(plan.real_params, "exposure",
                    std::to_string(scenario_model.camera.exposure_us.value()));
  }
  if (scenario_model.camera.gain_db.has_value()) {
    UpsertRealParam(plan.real_params, "gain",
                    FormatCompactDouble(scenario_model.camera.gain_db.value()));
  }
  if (scenario_model.camera.packet_size_bytes.has_value()) {
    UpsertRealParam(plan.real_params, "packet_size_bytes",
                    std::to_string(scenario_model.camera.packet_size_bytes.value()));
  }
  if (scenario_model.camera.inter_packet_delay_us.has_value()) {
    UpsertRealParam(plan.real_params, "inter_packet_delay_us",
                    std::to_string(scenario_model.camera.inter_packet_delay_us.value()));
  }
  if (scenario_model.camera.trigger_mode.has_value() &&
      !scenario_model.camera.trigger_mode->empty()) {
    UpsertRealParam(plan.real_params, "trigger_mode", scenario_model.camera.trigger_mode.value());
  }
  if (scenario_model.camera.trigger_source.has_value() &&
      !scenario_model.camera.trigger_source->empty()) {
    UpsertRealParam(plan.real_params, "trigger_source",
                    scenario_model.camera.trigger_source.value());
  }
  if (scenario_model.camera.trigger_activation.has_value() &&
      !scenario_model.camera.trigger_activation->empty()) {
    UpsertRealParam(plan.real_params, "trigger_activation",
                    scenario_model.camera.trigger_activation.value());
  }
  if (scenario_model.camera.roi.has_value()) {
    // Keep ROI ordering deterministic for cameras that require Width/Height
    // to be committed before OffsetX/OffsetY.
    UpsertRealParam(plan.real_params, "roi_width",
                    std::to_string(scenario_model.camera.roi->width));
    UpsertRealParam(plan.real_params, "roi_height",
                    std::to_string(scenario_model.camera.roi->height));
    UpsertRealParam(plan.real_params, "roi_offset_x", std::to_string(scenario_model.camera.roi->x));
    UpsertRealParam(plan.real_params, "roi_offset_y", std::to_string(scenario_model.camera.roi->y));
  }

  if (!assign_non_negative_double("min_avg_fps", scenario_model.thresholds.min_avg_fps,
                                  plan.thresholds.min_avg_fps)) {
    return false;
  }
  if (!assign_non_negative_double("max_drop_rate_percent",
                                  scenario_model.thresholds.max_drop_rate_percent,
                                  plan.thresholds.max_drop_rate_percent,
                                  /*percent_0_to_100=*/true)) {
    return false;
  }
  if (!assign_non_negative_double("max_inter_frame_interval_p95_us",
                                  scenario_model.thresholds.max_inter_frame_interval_p95_us,
                                  plan.thresholds.max_inter_frame_interval_p95_us)) {
    return false;
  }
  if (!assign_non_negative_double("max_inter_frame_jitter_p95_us",
                                  scenario_model.thresholds.max_inter_frame_jitter_p95_us,
                                  plan.thresholds.max_inter_frame_jitter_p95_us)) {
    return false;
  }
  if (!assign_non_negative_integer_threshold("max_disconnect_count",
                                             scenario_model.thresholds.max_disconnect_count,
                                             plan.thresholds.max_disconnect_count)) {
    return false;
  }

  if (scenario_model.netem_profile.has_value()) {
    if (scenario_model.netem_profile->empty()) {
      error = "scenario netem_profile must not be empty";
      return false;
    }
    if (!scenarios::IsLowercaseSlug(scenario_model.netem_profile.value())) {
      error = "scenario netem_profile must use lowercase slug format [a-z0-9_-]+";
      return false;
    }
    plan.netem_profile = scenario_model.netem_profile.value();
  }

  if (scenario_model.device_selector.has_value()) {
    if (scenario_model.device_selector->empty()) {
      error = "scenario device_selector must not be empty";
      return false;
    }
    backends::real_sdk::DeviceSelector parsed_selector;
    if (!backends::real_sdk::ParseDeviceSelector(scenario_model.device_selector.value(),
                                                 parsed_selector, error)) {
      error = "invalid scenario device_selector '" + scenario_model.device_selector.value() +
              "': " + error;
      return false;
    }
    plan.device_selector = scenario_model.device_selector.value();
  }

  if (plan.device_selector.has_value() && plan.backend != kBackendRealStub) {
    error = "device_selector requires backend real_stub";
    return false;
  }

  if (scenario_model.webcam.device_selector.has_value()) {
    backends::webcam::WebcamDeviceSelector selector;
    if (scenario_model.webcam.device_selector->id.has_value()) {
      selector.id = scenario_model.webcam.device_selector->id.value();
    }
    if (scenario_model.webcam.device_selector->name_contains.has_value()) {
      selector.name_contains = scenario_model.webcam.device_selector->name_contains.value();
    }
    if (scenario_model.webcam.device_selector->index.has_value()) {
      const std::uint64_t raw_index = scenario_model.webcam.device_selector->index.value();
      if (raw_index > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = "scenario webcam.device_selector.index is out of range";
        return false;
      }
      selector.index = static_cast<std::size_t>(raw_index);
    }
    plan.webcam_device_selector = std::move(selector);
  }

  if (plan.webcam_device_selector.has_value() && plan.backend != kBackendWebcam) {
    error = "webcam.device_selector requires backend webcam";
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
  run_info.config.backend = run_plan.backend;
  run_info.config.seed = run_plan.sim_config.seed;
  run_info.config.duration = run_plan.duration;
  run_info.timestamps.created_at = created_at;
  run_info.timestamps.started_at = created_at;
  run_info.timestamps.finished_at = created_at;
  return run_info;
}

bool BuildBackendFromRunPlan(const RunPlan& run_plan,
                             std::unique_ptr<backends::ICameraBackend>& backend,
                             std::string& error) {
  backend.reset();
  error.clear();
  if (run_plan.backend == kBackendSim) {
    backend = std::make_unique<backends::sim::SimCameraBackend>();
    return true;
  }
  if (run_plan.backend == kBackendWebcam) {
    backend = backends::webcam::CreateWebcamBackend();
    if (!backend) {
      error = "webcam backend not compiled on this platform";
      return false;
    }
    return true;
  }
  if (run_plan.backend == kBackendRealStub) {
    backend = backends::real_sdk::CreateRealBackend();
    return true;
  }

  error = "unsupported backend in run plan: " + run_plan.backend;
  return false;
}

struct ResolvedDeviceSelection {
  enum class Kind {
    kReal = 0,
    kWebcam,
  };

  Kind kind = Kind::kReal;
  std::string selector_text;
  std::string selection_rule;
  std::optional<backends::real_sdk::DeviceInfo> real_device;
  std::optional<backends::webcam::WebcamDeviceInfo> webcam_device;
  std::size_t discovered_index = 0;
};

void AttachResolvedDeviceMetadataToRunInfo(
    const std::optional<ResolvedDeviceSelection>& resolved_device_selection,
    core::schema::RunInfo& run_info) {
  run_info.real_device.reset();
  run_info.webcam_device.reset();
  if (!resolved_device_selection.has_value()) {
    return;
  }

  const ResolvedDeviceSelection& selected = resolved_device_selection.value();
  if (selected.kind == ResolvedDeviceSelection::Kind::kReal && selected.real_device.has_value()) {
    const backends::real_sdk::DeviceInfo& resolved_real = selected.real_device.value();
    core::schema::RealDeviceMetadata real_device;
    real_device.model = resolved_real.model;
    real_device.serial = resolved_real.serial;
    real_device.transport = resolved_real.transport;
    if (!resolved_real.user_id.empty()) {
      real_device.user_id = resolved_real.user_id;
    }
    real_device.firmware_version = resolved_real.firmware_version;
    real_device.sdk_version = resolved_real.sdk_version.value_or("unknown");
    run_info.real_device = std::move(real_device);
    return;
  }

  if (selected.kind == ResolvedDeviceSelection::Kind::kWebcam &&
      selected.webcam_device.has_value()) {
    const backends::webcam::WebcamDeviceInfo& resolved_webcam = selected.webcam_device.value();
    core::schema::WebcamDeviceMetadata webcam_device;
    webcam_device.device_id = resolved_webcam.device_id;
    webcam_device.friendly_name = resolved_webcam.friendly_name;
    webcam_device.bus_info = resolved_webcam.bus_info;
    if (!selected.selector_text.empty()) {
      webcam_device.selector_text = selected.selector_text;
    }
    if (!selected.selection_rule.empty()) {
      webcam_device.selection_rule = selected.selection_rule;
    }
    webcam_device.discovered_index = static_cast<std::uint64_t>(selected.discovered_index);
    run_info.webcam_device = std::move(webcam_device);
  }
}

core::schema::TransportCounterStatus
ToTransportCounterStatus(const backends::real_sdk::TransportCounterReading& reading) {
  core::schema::TransportCounterStatus status;
  status.available = reading.available;
  if (reading.available) {
    status.value = reading.value;
  }
  return status;
}

void AttachTransportCountersToRunInfo(const backends::BackendConfig& backend_dump,
                                      core::schema::RunInfo& run_info) {
  if (!run_info.real_device.has_value()) {
    return;
  }

  const backends::real_sdk::TransportCountersSnapshot counters =
      backends::real_sdk::CollectTransportCounters(backend_dump);
  run_info.real_device->transport_counters.resends = ToTransportCounterStatus(counters.resends);
  run_info.real_device->transport_counters.packet_errors =
      ToTransportCounterStatus(counters.packet_errors);
  run_info.real_device->transport_counters.dropped_packets =
      ToTransportCounterStatus(counters.dropped_packets);
}

bool ResolveDeviceSelectionForRun(const RunPlan& run_plan, const RunOptions& options,
                                  std::optional<ResolvedDeviceSelection>& resolved,
                                  std::string& error) {
  resolved.reset();
  error.clear();

  std::optional<std::string> selector_text;
  if (!options.device_selector.empty()) {
    selector_text = options.device_selector;
  } else if (run_plan.device_selector.has_value()) {
    selector_text = run_plan.device_selector.value();
  }

  if (run_plan.backend == kBackendRealStub) {
    if (!selector_text.has_value()) {
      return true;
    }

    backends::real_sdk::DeviceInfo selected_device;
    std::size_t selected_index = 0;
    if (!backends::real_sdk::ResolveConnectedDevice(selector_text.value(), selected_device,
                                                    selected_index, error)) {
      return false;
    }

    resolved = ResolvedDeviceSelection{
        .kind = ResolvedDeviceSelection::Kind::kReal,
        .selector_text = selector_text.value(),
        .selection_rule = "real_selector",
        .real_device = std::move(selected_device),
        .discovered_index = selected_index,
    };
    return true;
  }

  if (run_plan.backend == kBackendWebcam) {
    backends::webcam::WebcamDeviceSelector webcam_selector;
    std::string webcam_selector_text;
    if (!options.device_selector.empty()) {
      if (!backends::webcam::ParseWebcamDeviceSelector(options.device_selector, webcam_selector,
                                                       error)) {
        error = "invalid webcam --device selector '" + options.device_selector + "': " + error;
        return false;
      }
      webcam_selector_text = options.device_selector;
    } else if (run_plan.webcam_device_selector.has_value()) {
      webcam_selector = run_plan.webcam_device_selector.value();
      if (webcam_selector.id.has_value()) {
        webcam_selector_text = "id:" + webcam_selector.id.value();
      } else if (webcam_selector.index.has_value()) {
        webcam_selector_text = "index:" + std::to_string(webcam_selector.index.value());
      } else if (webcam_selector.name_contains.has_value()) {
        webcam_selector_text = "name_contains:" + webcam_selector.name_contains.value();
      }
    }

    std::vector<backends::webcam::WebcamDeviceInfo> devices;
    if (!backends::webcam::EnumerateConnectedDevices(devices, error)) {
      return false;
    }

    backends::webcam::WebcamSelectionResult webcam_result;
    if (!backends::webcam::ResolveWebcamDeviceSelector(devices, webcam_selector, webcam_result,
                                                       error)) {
      return false;
    }

    resolved = ResolvedDeviceSelection{
        .kind = ResolvedDeviceSelection::Kind::kWebcam,
        .selector_text = webcam_selector_text.empty() ? std::string("default:index:0")
                                                      : std::move(webcam_selector_text),
        .selection_rule = std::string(backends::webcam::ToString(webcam_result.rule)),
        .webcam_device = webcam_result.device,
        .discovered_index = webcam_result.index,
    };
    return true;
  }

  if (selector_text.has_value()) {
    error = "--device/device_selector requires backend real_stub or webcam";
    return false;
  }
  return true;
}

bool ApplyDeviceSelectionToBackend(backends::ICameraBackend& backend,
                                   const ResolvedDeviceSelection& selection,
                                   backends::BackendConfig& applied_params, std::string& error) {
  auto apply = [&](std::string key, std::string value) {
    if (!backend.SetParam(key, value, error)) {
      return false;
    }
    applied_params[std::move(key)] = std::move(value);
    return true;
  };

  if (!apply("device.selector", selection.selector_text) ||
      !apply("device.selection_rule", selection.selection_rule) ||
      !apply("device.index", std::to_string(selection.discovered_index))) {
    return false;
  }

  if (selection.kind == ResolvedDeviceSelection::Kind::kReal && selection.real_device.has_value()) {
    const backends::real_sdk::DeviceInfo& device = selection.real_device.value();
    if (!apply("device.model", device.model) || !apply("device.serial", device.serial) ||
        !apply("device.user_id", device.user_id.empty() ? "(none)" : device.user_id) ||
        !apply("device.transport", device.transport)) {
      return false;
    }

    if (device.ip_address.has_value() && !apply("device.ip", device.ip_address.value())) {
      return false;
    }
    if (device.mac_address.has_value() && !apply("device.mac", device.mac_address.value())) {
      return false;
    }
    if (device.firmware_version.has_value() &&
        !apply("device.firmware_version", device.firmware_version.value())) {
      return false;
    }
    if (device.sdk_version.has_value() &&
        !apply("device.sdk_version", device.sdk_version.value())) {
      return false;
    }
    return true;
  }

  if (selection.kind == ResolvedDeviceSelection::Kind::kWebcam &&
      selection.webcam_device.has_value()) {
    const backends::webcam::WebcamDeviceInfo& device = selection.webcam_device.value();
    if (!apply("device.id", device.device_id) ||
        !apply("device.friendly_name", device.friendly_name)) {
      return false;
    }
    if (device.bus_info.has_value() && !apply("device.bus_info", device.bus_info.value())) {
      return false;
    }
    return true;
  }

  return true;
}

bool ConfigureOptionalSdkLogCapture(const RunOptions& options, const RunPlan& run_plan,
                                    backends::ICameraBackend& backend, const fs::path& bundle_dir,
                                    fs::path& sdk_log_path, core::logging::Logger& logger,
                                    std::string& error) {
  // Keep SDK capture opt-in so default runs remain lightweight. When enabled,
  // pass a stable bundle path into the backend so vendor-level logs land next
  // to run artifacts without introducing backend-specific wiring in callers.
  sdk_log_path.clear();
  if (!options.capture_sdk_log) {
    return true;
  }

  if (run_plan.backend != kBackendRealStub) {
    logger.Warn("sdk log capture requested for non-real backend; request ignored",
                {{"backend", run_plan.backend}});
    return true;
  }

  sdk_log_path = bundle_dir / "sdk_log.txt";
  if (!backend.SetParam("sdk.log.path", sdk_log_path.string(), error)) {
    error = "failed to enable sdk log capture: " + error;
    return false;
  }
  logger.Info("sdk log capture enabled", {{"sdk_log_path", sdk_log_path.string()}});
  return true;
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
                      std::map<std::string, std::string> payload, const fs::path& output_dir,
                      fs::path& events_path, std::string& error) {
  events::Emitter emitter(output_dir, events_path);
  return emitter.EmitRaw(type, ts, std::move(payload), error);
}

std::string ToLowerAscii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

struct RealFailureDetails {
  std::string code;
  std::string actionable_message;
  std::string formatted_message;
};

RealFailureDetails MapRealFailure(std::string_view operation, std::string_view raw_error) {
  const auto mapped = backends::real_sdk::MapRealBackendError(operation, raw_error);
  RealFailureDetails details;
  details.code = std::string(backends::real_sdk::ToStableErrorCode(mapped.code));
  details.actionable_message = mapped.actionable_message;
  details.formatted_message = backends::real_sdk::FormatRealBackendError(operation, raw_error);
  return details;
}

bool IsGigETransport(const core::schema::RunInfo& run_info) {
  if (!run_info.real_device.has_value()) {
    return false;
  }
  return ToLowerAscii(run_info.real_device->transport) == "gige";
}

bool IsGigEOnlyTransportTuningKey(std::string_view generic_key) {
  return generic_key == "packet_size_bytes" || generic_key == "inter_packet_delay_us";
}

bool ApplyRealParamsWithEvents(backends::ICameraBackend& backend, const RunPlan& run_plan,
                               const core::schema::RunInfo& run_info, const fs::path& bundle_dir,
                               backends::BackendConfig& applied_params, fs::path& events_path,
                               fs::path& config_verify_path, fs::path& camera_config_path,
                               fs::path& config_report_path, core::logging::Logger& logger,
                               std::string& error) {
  config_verify_path.clear();
  camera_config_path.clear();
  config_report_path.clear();
  events::Emitter emitter(bundle_dir, events_path);

  backends::real_sdk::ApplyParamsResult apply_result;
  auto write_human_config_reports = [&](std::string_view collection_error,
                                        std::string& write_error) -> bool {
    const backends::BackendConfig backend_dump = backend.DumpConfig();
    if (!artifacts::WriteCameraConfigJson(run_info, backend_dump, run_plan.real_params,
                                          apply_result, run_plan.real_apply_mode, collection_error,
                                          bundle_dir, camera_config_path, write_error)) {
      write_error = "failed to write camera_config.json: " + write_error;
      return false;
    }
    if (!artifacts::WriteConfigReportMarkdown(run_info, run_plan.real_params, apply_result,
                                              run_plan.real_apply_mode, collection_error,
                                              bundle_dir, config_report_path, write_error)) {
      write_error = "failed to write config_report.md: " + write_error;
      return false;
    }
    return true;
  };

  const fs::path key_map_path = backends::real_sdk::ResolveDefaultParamKeyMapPath();
  backends::real_sdk::ParamKeyMap key_map;
  if (!backends::real_sdk::LoadParamKeyMapFromFile(key_map_path, key_map, error)) {
    std::string write_error;
    if (!write_human_config_reports(error, write_error)) {
      error = std::move(write_error);
      return false;
    }
    error = "failed to load real backend param key map: " + error;
    return false;
  }

  std::unique_ptr<backends::real_sdk::INodeMapAdapter> adapter =
      backends::real_sdk::CreateDefaultNodeMapAdapter();
  if (adapter == nullptr) {
    const std::string adapter_error = "real backend node-map adapter initialization failed";
    std::string write_error;
    if (!write_human_config_reports(adapter_error, write_error)) {
      error = std::move(write_error);
      return false;
    }
    error = "failed to initialize real backend node-map adapter";
    return false;
  }

  std::vector<backends::real_sdk::ApplyParamInput> params_for_apply;
  std::vector<backends::real_sdk::ApplyParamInput> skipped_transport_tuning;
  params_for_apply.reserve(run_plan.real_params.size());
  const bool is_gige_transport = IsGigETransport(run_info);
  for (const auto& param : run_plan.real_params) {
    if (!is_gige_transport && IsGigEOnlyTransportTuningKey(param.generic_key)) {
      skipped_transport_tuning.push_back(param);
      continue;
    }
    params_for_apply.push_back(param);
  }

  const std::string resolved_transport =
      run_info.real_device.has_value() ? run_info.real_device->transport : "unknown";
  auto append_skipped_transport_tuning_rows = [&]() {
    for (const auto& skipped : skipped_transport_tuning) {
      const std::string reason =
          "setting requires GigE transport (resolved transport: " + resolved_transport + ")";
      apply_result.unsupported.push_back(backends::real_sdk::UnsupportedParam{
          .generic_key = skipped.generic_key,
          .requested_value = skipped.requested_value,
          .reason = reason,
      });
      apply_result.readback_rows.push_back(backends::real_sdk::ReadbackRow{
          .generic_key = skipped.generic_key,
          .requested_value = skipped.requested_value,
          .supported = false,
          .applied = false,
          .reason = reason,
      });
    }
  };
  if (!skipped_transport_tuning.empty()) {
    logger.Info("skipping transport tuning keys for non-gige transport",
                {{"resolved_transport", resolved_transport},
                 {"skipped_count", std::to_string(skipped_transport_tuning.size())}});
  }

  if (!backends::real_sdk::ApplyParams(backend, key_map, *adapter, params_for_apply,
                                       run_plan.real_apply_mode, apply_result, error)) {
    append_skipped_transport_tuning_rows();
    std::string write_error;
    if (!artifacts::WriteConfigVerifyJson(run_info, apply_result, run_plan.real_apply_mode,
                                          bundle_dir, config_verify_path, write_error)) {
      error = "failed to write config_verify.json: " + write_error;
      return false;
    }
    if (!write_human_config_reports(error, write_error)) {
      error = std::move(write_error);
      return false;
    }
    for (const auto& unsupported : apply_result.unsupported) {
      std::string event_error;
      if (!emitter.EmitConfigStatus(
              {
                  .kind = events::Emitter::ConfigStatusEvent::Kind::kUnsupported,
                  .ts = std::chrono::system_clock::now(),
                  .run_id = run_info.run_id,
                  .scenario_id = run_info.config.scenario_id,
                  .apply_mode = backends::real_sdk::ToString(run_plan.real_apply_mode),
                  .generic_key = unsupported.generic_key,
                  .requested_value = unsupported.requested_value,
                  .reason = unsupported.reason,
              },
              event_error)) {
        logger.Warn("failed to append CONFIG_UNSUPPORTED event on strict apply failure",
                    {{"error", event_error}});
      }
    }
    return false;
  }
  append_skipped_transport_tuning_rows();

  for (const auto& unsupported : apply_result.unsupported) {
    if (!emitter.EmitConfigStatus(
            {
                .kind = events::Emitter::ConfigStatusEvent::Kind::kUnsupported,
                .ts = std::chrono::system_clock::now(),
                .run_id = run_info.run_id,
                .scenario_id = run_info.config.scenario_id,
                .apply_mode = backends::real_sdk::ToString(run_plan.real_apply_mode),
                .generic_key = unsupported.generic_key,
                .requested_value = unsupported.requested_value,
                .reason = unsupported.reason,
            },
            error)) {
      return false;
    }
    logger.Warn("config unsupported in best-effort mode",
                {{"generic_key", unsupported.generic_key}, {"reason", unsupported.reason}});
  }

  for (const auto& applied : apply_result.applied) {
    applied_params[applied.generic_key] = applied.applied_value;
    if (!applied.adjusted) {
      continue;
    }
    if (!emitter.EmitConfigStatus(
            {
                .kind = events::Emitter::ConfigStatusEvent::Kind::kAdjusted,
                .ts = std::chrono::system_clock::now(),
                .run_id = run_info.run_id,
                .scenario_id = run_info.config.scenario_id,
                .apply_mode = backends::real_sdk::ToString(run_plan.real_apply_mode),
                .generic_key = applied.generic_key,
                .requested_value = applied.requested_value,
                .reason = applied.adjustment_reason,
                .node_name = applied.node_name,
                .applied_value = applied.applied_value,
            },
            error)) {
      return false;
    }
  }

  if (!artifacts::WriteConfigVerifyJson(run_info, apply_result, run_plan.real_apply_mode,
                                        bundle_dir, config_verify_path, error)) {
    error = "failed to write config_verify.json: " + error;
    return false;
  }
  std::string write_error;
  if (!write_human_config_reports(/*collection_error=*/"", write_error)) {
    error = std::move(write_error);
    return false;
  }

  return true;
}

// Evaluates scenario pass/fail thresholds against computed metrics.
// Returns true when all configured thresholds pass and appends actionable
// failure reasons otherwise.
bool EvaluateRunThresholds(const RunPlan::Thresholds& thresholds, const metrics::FpsReport& report,
                           std::vector<std::string>& failures) {
  failures.clear();

  auto check_min = [&](std::string_view label, double actual,
                       const std::optional<double>& minimum) {
    if (!minimum.has_value()) {
      return;
    }
    if (actual + 1e-9 < minimum.value()) {
      failures.push_back(std::string(label) + " actual=" + std::to_string(actual) +
                         " is below minimum=" + std::to_string(minimum.value()));
    }
  };

  auto check_max = [&](std::string_view label, double actual,
                       const std::optional<double>& maximum) {
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
  check_max("inter_frame_interval_p95_us", report.inter_frame_interval_us.p95_us,
            thresholds.max_inter_frame_interval_p95_us);
  check_max("inter_frame_jitter_p95_us", report.inter_frame_jitter_us.p95_us,
            thresholds.max_inter_frame_jitter_p95_us);

  if (thresholds.max_disconnect_count.has_value()) {
    constexpr std::uint64_t kObservedDisconnectCount = 0;
    if (kObservedDisconnectCount > thresholds.max_disconnect_count.value()) {
      failures.push_back(
          "disconnect_count actual=" + std::to_string(kObservedDisconnectCount) +
          " exceeds maximum=" + std::to_string(thresholds.max_disconnect_count.value()));
    }
  }

  return failures.empty();
}

std::string ResolveSoakStopReason(const RunOptions& options) {
  if (g_run_interrupt_requested.load()) {
    return "signal_interrupt";
  }
  if (!options.soak_stop_file.empty()) {
    std::error_code ec;
    if (fs::exists(options.soak_stop_file, ec) && !ec) {
      return "stop_file_detected";
    }
  }
  return "";
}

class ScopedInterruptSignalHandler {
public:
  ScopedInterruptSignalHandler() {
    g_run_interrupt_requested.store(false);
    previous_handler_ = std::signal(SIGINT, HandleInterruptSignal);
  }

  ~ScopedInterruptSignalHandler() {
    (void)std::signal(SIGINT, previous_handler_);
  }

  ScopedInterruptSignalHandler(const ScopedInterruptSignalHandler&) = delete;
  ScopedInterruptSignalHandler& operator=(const ScopedInterruptSignalHandler&) = delete;

private:
  using SignalHandler = void (*)(int);
  SignalHandler previous_handler_ = SIG_DFL;
};

std::vector<fs::path> CollectNicRawArtifactPaths(const fs::path& bundle_dir) {
  std::vector<fs::path> paths;
  std::error_code ec;
  if (!fs::exists(bundle_dir, ec) || ec) {
    return paths;
  }

  for (const auto& entry : fs::directory_iterator(bundle_dir, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("nic_", 0U) == 0U && entry.path().extension() == ".txt") {
      paths.push_back(entry.path());
    }
  }

  std::sort(paths.begin(), paths.end());
  return paths;
}

// Shared mutable state for one scenario execution. This keeps stage boundaries
// explicit while preserving the existing run artifacts and exit-code contract.
struct RunExecutionContext {
  explicit RunExecutionContext(core::logging::LogLevel log_level) : logger(log_level) {}

  core::logging::Logger logger;
  std::string error;
  RunPlan run_plan;
  std::optional<ResolvedDeviceSelection> resolved_device_selection;
  std::optional<artifacts::NetemCommandSuggestions> netem_suggestions;
  bool is_resume = false;
  soak::CheckpointState resume_checkpoint;
  std::chrono::milliseconds completed_duration{0};
  std::vector<backends::FrameSample> frames;
  core::schema::RunInfo run_info;
  fs::path bundle_dir;
  fs::path soak_frame_cache_path;
  fs::path soak_checkpoint_latest_path;
  fs::path soak_checkpoint_history_path;
  fs::path scenario_artifact_path;
  fs::path hostprobe_artifact_path;
  std::vector<fs::path> hostprobe_raw_artifact_paths;
  std::unique_ptr<backends::ICameraBackend> backend;
  std::unique_ptr<ScopedNetemTeardown> netem_teardown_guard;
  fs::path sdk_log_artifact_path;
  backends::BackendConfig selected_device_params;
  fs::path events_path;
  fs::path config_verify_artifact_path;
  fs::path camera_config_artifact_path;
  fs::path config_report_artifact_path;
  backends::BackendConfig applied_params;
  bool config_applied_event_emitted = false;
  bool stream_started = false;
  std::uint64_t dropped_count = 0;
  std::uint64_t received_count = 0;
  std::optional<std::chrono::system_clock::time_point> latest_frame_ts;
  bool interrupted_by_signal = false;
  std::chrono::milliseconds non_soak_completed_duration{0};
  bool disconnect_failure = false;
  std::uint32_t reconnect_attempts_used = 0;
  std::string disconnect_failure_error;
  bool soak_paused = false;
  fs::path run_artifact_path;
  metrics::FpsReport fps_report;
  fs::path metrics_csv_path;
  fs::path metrics_json_path;
  bool thresholds_passed = true;
  std::vector<std::string> threshold_failures;
  std::vector<std::string> top_anomalies;
  fs::path summary_markdown_path;
  fs::path report_html_path;
  fs::path bundle_manifest_path;
  fs::path bundle_zip_path;
};

constexpr std::uint32_t kReconnectRetryLimit = backends::real_sdk::kDefaultReconnectRetryLimit;

void StopBackendIfStreamStarted(RunExecutionContext& ctx) {
  if (!ctx.stream_started || ctx.backend == nullptr) {
    return;
  }
  std::string stop_error;
  (void)ctx.backend->Stop(stop_error);
  ctx.stream_started = false;
}

int PrepareRunContext(const RunOptions& options, bool use_per_run_bundle_dir, bool allow_zip_bundle,
                      ScenarioRunResult* run_result, RunExecutionContext& ctx) {
  ctx.logger.Info(
      "run execution requested",
      {{"scenario_path", options.scenario_path},
       {"output_root", options.output_dir.string()},
       {"zip_bundle", options.zip_bundle ? "true" : "false"},
       {"redact", options.redact_identifiers ? "true" : "false"},
       {"sdk_log", options.capture_sdk_log ? "true" : "false"},
       {"soak_mode", options.soak_mode ? "true" : "false"},
       {"netem_apply", options.apply_netem ? "true" : "false"},
       {"device_selector", options.device_selector.empty() ? "-" : options.device_selector}});

  if (options.soak_mode && !use_per_run_bundle_dir) {
    ctx.logger.Error("soak mode is only supported for per-run bundle execution");
    std::cerr << "error: soak mode is only supported by labops run\n";
    return kExitUsage;
  }
  if (options.soak_mode && options.checkpoint_interval <= std::chrono::milliseconds::zero()) {
    ctx.logger.Error("invalid soak checkpoint interval");
    std::cerr << "error: checkpoint interval must be greater than 0 milliseconds\n";
    return kExitUsage;
  }
  if (options.zip_bundle && !allow_zip_bundle) {
    ctx.logger.Error("zip output is not supported for this command");
    std::cerr << "error: zip output is not supported for this command\n";
    return kExitUsage;
  }

  if (!ValidateScenarioPath(options.scenario_path, ctx.error)) {
    ctx.logger.Error("scenario path validation failed",
                     {{"scenario_path", options.scenario_path}, {"error", ctx.error}});
    std::cerr << "error: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!LoadRunPlanFromScenario(options.scenario_path, ctx.run_plan, ctx.error)) {
    ctx.logger.Error("failed to load run plan from scenario",
                     {{"scenario_path", options.scenario_path}, {"error", ctx.error}});
    std::cerr << "error: " << ctx.error << '\n';
    return kExitSchemaInvalid;
  }

  if (!ResolveDeviceSelectionForRun(ctx.run_plan, options, ctx.resolved_device_selection,
                                    ctx.error)) {
    ctx.logger.Error("device selector resolution failed", {{"error", ctx.error}});
    std::cerr << "error: device selector resolution failed: " << ctx.error << '\n';
    return kExitFailure;
  }
  if (ctx.resolved_device_selection.has_value()) {
    const ResolvedDeviceSelection& selected = ctx.resolved_device_selection.value();
    if (selected.kind == ResolvedDeviceSelection::Kind::kReal && selected.real_device.has_value()) {
      const backends::real_sdk::DeviceInfo& device = selected.real_device.value();
      ctx.logger.Info("device selector resolved",
                      {{"selector", selected.selector_text},
                       {"selected_index", std::to_string(selected.discovered_index)},
                       {"selected_model", device.model},
                       {"selected_serial", device.serial},
                       {"selected_user_id", device.user_id.empty() ? "(none)" : device.user_id},
                       {"selected_transport", device.transport}});
      if (device.firmware_version.has_value()) {
        ctx.logger.Info("device selector firmware detected",
                        {{"selected_firmware_version", device.firmware_version.value()}});
      }
      if (device.sdk_version.has_value()) {
        ctx.logger.Info("device selector sdk version detected",
                        {{"selected_sdk_version", device.sdk_version.value()}});
      }
    } else if (selected.kind == ResolvedDeviceSelection::Kind::kWebcam &&
               selected.webcam_device.has_value()) {
      const backends::webcam::WebcamDeviceInfo& device = selected.webcam_device.value();
      ctx.logger.Info("webcam device selector resolved",
                      {{"selector", selected.selector_text},
                       {"selection_rule", selected.selection_rule},
                       {"selected_index", std::to_string(selected.discovered_index)},
                       {"selected_device_id", device.device_id},
                       {"selected_friendly_name", device.friendly_name},
                       {"selected_bus_info", device.bus_info.value_or("(none)")}});
    }
  }

  std::string netem_warning;
  if (!BuildNetemCommandSuggestions(options.scenario_path, ctx.run_plan, ctx.netem_suggestions,
                                    netem_warning)) {
    ctx.logger.Error("failed to build netem command suggestions");
    std::cerr << "error: failed to build netem command suggestions\n";
    return kExitFailure;
  }
  if (!netem_warning.empty()) {
    ctx.logger.Warn("netem suggestion warning", {{"warning", netem_warning}});
    std::cerr << "warning: " << netem_warning << '\n';
  }

  ctx.is_resume = options.soak_mode && !options.resume_checkpoint_path.empty();
  const auto created_at = std::chrono::system_clock::now();
  ctx.run_info = BuildRunInfo(options, ctx.run_plan, created_at);
  AttachResolvedDeviceMetadataToRunInfo(ctx.resolved_device_selection, ctx.run_info);
  ctx.bundle_dir = ResolveExecutionOutputDir(options, ctx.run_info, use_per_run_bundle_dir);

  if (ctx.is_resume) {
    if (!soak::LoadCheckpoint(options.resume_checkpoint_path, ctx.resume_checkpoint, ctx.error)) {
      ctx.logger.Error(
          "failed to load soak checkpoint",
          {{"checkpoint", options.resume_checkpoint_path.string()}, {"error", ctx.error}});
      std::cerr << "error: failed to load soak checkpoint: " << ctx.error << '\n';
      return kExitFailure;
    }

    if (fs::path(options.scenario_path).lexically_normal() !=
        ctx.resume_checkpoint.scenario_path.lexically_normal()) {
      ctx.logger.Error("resume scenario mismatch",
                       {{"scenario_path", options.scenario_path},
                        {"checkpoint_scenario", ctx.resume_checkpoint.scenario_path.string()}});
      std::cerr << "error: resume scenario mismatch: expected "
                << ctx.resume_checkpoint.scenario_path.string() << '\n';
      return kExitFailure;
    }
    if (ctx.resume_checkpoint.status == soak::CheckpointStatus::kCompleted) {
      ctx.logger.Error("resume requested for already completed checkpoint");
      std::cerr << "error: checkpoint is already completed\n";
      return kExitFailure;
    }
    if (ctx.resume_checkpoint.completed_duration >= ctx.resume_checkpoint.total_duration) {
      ctx.logger.Error("resume requested but checkpoint has no remaining duration");
      std::cerr << "error: checkpoint has no remaining soak duration\n";
      return kExitFailure;
    }
    if (ctx.run_plan.duration.count() != ctx.resume_checkpoint.total_duration.count()) {
      ctx.logger.Error("resume duration mismatch",
                       {{"scenario_duration_ms", std::to_string(ctx.run_plan.duration.count())},
                        {"checkpoint_duration_ms",
                         std::to_string(ctx.resume_checkpoint.total_duration.count())}});
      std::cerr << "error: scenario duration does not match checkpoint duration\n";
      return kExitFailure;
    }

    ctx.run_info.run_id = ctx.resume_checkpoint.run_id;
    ctx.run_info.timestamps.created_at = ctx.resume_checkpoint.timestamps.created_at;
    ctx.run_info.timestamps.started_at = ctx.resume_checkpoint.timestamps.started_at;
    ctx.run_info.timestamps.finished_at = ctx.resume_checkpoint.timestamps.finished_at;
    ctx.completed_duration = ctx.resume_checkpoint.completed_duration;
    ctx.bundle_dir = ctx.resume_checkpoint.bundle_dir;
    ctx.soak_frame_cache_path = ctx.resume_checkpoint.frame_cache_path.empty()
                                    ? (ctx.bundle_dir / "soak_frames.jsonl")
                                    : ctx.resume_checkpoint.frame_cache_path;

    if (!soak::LoadFrameCache(ctx.soak_frame_cache_path, ctx.frames, ctx.error)) {
      ctx.logger.Error("failed to load soak frame cache",
                       {{"path", ctx.soak_frame_cache_path.string()}, {"error", ctx.error}});
      std::cerr << "error: failed to load soak frame cache: " << ctx.error << '\n';
      return kExitFailure;
    }
  } else if (options.soak_mode) {
    ctx.soak_frame_cache_path = ctx.bundle_dir / "soak_frames.jsonl";
  }

  ctx.logger.SetRunId(ctx.run_info.run_id);
  ctx.logger.Info("run initialized",
                  {{"scenario_id", ctx.run_info.config.scenario_id},
                   {"backend", ctx.run_info.config.backend},
                   {"bundle_dir", ctx.bundle_dir.string()},
                   {"duration_ms", std::to_string(ctx.run_plan.duration.count())}});
  if (run_result != nullptr) {
    run_result->run_id = ctx.run_info.run_id;
    run_result->bundle_dir = ctx.bundle_dir;
  }
  return kExitSuccess;
}

int InitializeArtifacts(const RunOptions& options, RunExecutionContext& ctx) {
  ctx.scenario_artifact_path = ctx.bundle_dir / "scenario.json";
  if (!ctx.is_resume || !fs::exists(ctx.scenario_artifact_path)) {
    if (!artifacts::WriteScenarioJson(options.scenario_path, ctx.bundle_dir,
                                      ctx.scenario_artifact_path, ctx.error)) {
      ctx.logger.Error("failed to write scenario snapshot",
                       {{"bundle_dir", ctx.bundle_dir.string()}, {"error", ctx.error}});
      std::cerr << "error: failed to write scenario snapshot: " << ctx.error << '\n';
      return kExitFailure;
    }
    ctx.logger.Debug("scenario snapshot written", {{"path", ctx.scenario_artifact_path.string()}});
  } else {
    ctx.logger.Info("resume mode reusing existing scenario snapshot",
                    {{"path", ctx.scenario_artifact_path.string()}});
  }

  ctx.hostprobe_artifact_path = ctx.bundle_dir / "hostprobe.json";
  if (ctx.is_resume && fs::exists(ctx.hostprobe_artifact_path)) {
    ctx.hostprobe_raw_artifact_paths = CollectNicRawArtifactPaths(ctx.bundle_dir);
    ctx.logger.Info(
        "resume mode reusing existing host probe artifacts",
        {{"hostprobe", ctx.hostprobe_artifact_path.string()},
         {"hostprobe_raw_count", std::to_string(ctx.hostprobe_raw_artifact_paths.size())}});
    return kExitSuccess;
  }

  hostprobe::HostProbeSnapshot host_snapshot;
  if (!hostprobe::CollectHostProbeSnapshot(host_snapshot, ctx.error)) {
    ctx.logger.Error("failed to collect host probe data", {{"error", ctx.error}});
    std::cerr << "error: failed to collect host probe data: " << ctx.error << '\n';
    return kExitFailure;
  }

  hostprobe::NicProbeSnapshot nic_probe;
  if (!hostprobe::CollectNicProbeSnapshot(nic_probe, ctx.error)) {
    ctx.logger.Warn("NIC probe collection issue", {{"warning", ctx.error}});
    std::cerr << "warning: NIC probe collection issue: " << ctx.error << '\n';
  }
  host_snapshot.nic_highlights = nic_probe.highlights;

  if (options.redact_identifiers) {
    hostprobe::IdentifierRedactionContext redaction_context;
    hostprobe::BuildIdentifierRedactionContext(redaction_context);
    hostprobe::RedactHostProbeSnapshot(host_snapshot, redaction_context);
    hostprobe::RedactNicProbeSnapshot(nic_probe, redaction_context);
    host_snapshot.nic_highlights = nic_probe.highlights;
  }

  if (!artifacts::WriteHostProbeJson(host_snapshot, ctx.bundle_dir, ctx.hostprobe_artifact_path,
                                     ctx.error)) {
    ctx.logger.Error("failed to write host probe artifact", {{"error", ctx.error}});
    std::cerr << "error: failed to write hostprobe.json: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!artifacts::WriteHostProbeRawCommandOutputs(nic_probe.raw_captures, ctx.bundle_dir,
                                                  ctx.hostprobe_raw_artifact_paths, ctx.error)) {
    ctx.logger.Error("failed to write NIC raw command artifacts", {{"error", ctx.error}});
    std::cerr << "error: failed to write NIC raw command artifacts: " << ctx.error << '\n';
    return kExitFailure;
  }
  return kExitSuccess;
}

int ConfigureBackend(const RunOptions& options, ScenarioRunResult* run_result,
                     RunExecutionContext& ctx) {
  events::Emitter emitter(ctx.bundle_dir, ctx.events_path);

  if (!BuildBackendFromRunPlan(ctx.run_plan, ctx.backend, ctx.error)) {
    ctx.logger.Error("backend selection failed", {{"error", ctx.error}});
    std::cerr << "error: backend selection failed: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!ConfigureOptionalSdkLogCapture(options, ctx.run_plan, *ctx.backend, ctx.bundle_dir,
                                      ctx.sdk_log_artifact_path, ctx.logger, ctx.error)) {
    ctx.logger.Error("failed to configure sdk log capture", {{"error", ctx.error}});
    std::cerr << "error: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (ctx.resolved_device_selection.has_value() &&
      !ApplyDeviceSelectionToBackend(*ctx.backend, ctx.resolved_device_selection.value(),
                                     ctx.selected_device_params, ctx.error)) {
    ctx.logger.Error("failed to apply resolved device selector", {{"error", ctx.error}});
    std::cerr << "error: failed to apply resolved device selector: " << ctx.error << '\n';
    return kExitFailure;
  }

  for (const auto& [key, value] : ctx.selected_device_params) {
    ctx.applied_params[key] = value;
  }

  if (ctx.run_plan.backend == kBackendRealStub) {
    if (!ApplyRealParamsWithEvents(*ctx.backend, ctx.run_plan, ctx.run_info, ctx.bundle_dir,
                                   ctx.applied_params, ctx.events_path,
                                   ctx.config_verify_artifact_path, ctx.camera_config_artifact_path,
                                   ctx.config_report_artifact_path, ctx.logger, ctx.error)) {
      ctx.logger.Error("backend config apply failed", {{"error", ctx.error}});
      std::cerr << "error: backend config failed: " << ctx.error << '\n';
      return kExitFailure;
    }

    if (!emitter.EmitConfigStatus(
            {
                .kind = events::Emitter::ConfigStatusEvent::Kind::kApplied,
                .ts = std::chrono::system_clock::now(),
                .run_id = ctx.run_info.run_id,
                .scenario_id = ctx.run_info.config.scenario_id,
                .applied_params = ctx.applied_params,
            },
            ctx.error)) {
      ctx.logger.Error("failed to append CONFIG_APPLIED event", {{"error", ctx.error}});
      std::cerr << "error: failed to append CONFIG_APPLIED event: " << ctx.error << '\n';
      return kExitFailure;
    }
    ctx.config_applied_event_emitted = true;
  }

  if (!ctx.backend->Connect(ctx.error)) {
    std::optional<RealFailureDetails> mapped_connect_error;
    if (ctx.run_plan.backend == kBackendRealStub) {
      mapped_connect_error = MapRealFailure("connect", ctx.error);
      ctx.logger.Error("backend connect failed",
                       {{"backend", ctx.run_info.config.backend},
                        {"error_code", mapped_connect_error->code},
                        {"error_action", mapped_connect_error->actionable_message},
                        {"error", ctx.error}});
    } else {
      ctx.logger.Error("backend connect failed",
                       {{"backend", ctx.run_info.config.backend}, {"error", ctx.error}});
    }
    ctx.run_info.timestamps.finished_at = std::chrono::system_clock::now();
    if (ctx.run_plan.backend == kBackendRealStub) {
      AttachTransportCountersToRunInfo(ctx.backend->DumpConfig(), ctx.run_info);
    }

    std::string run_write_error;
    if (!artifacts::WriteRunJson(ctx.run_info, ctx.bundle_dir, ctx.run_artifact_path,
                                 run_write_error)) {
      ctx.logger.Error("failed to write run.json after backend connect failure",
                       {{"error", run_write_error}});
      std::cerr << "warning: failed to write run.json after backend connect failure: "
                << run_write_error << '\n';
    } else if (run_result != nullptr) {
      run_result->run_json_path = ctx.run_artifact_path;
    }
    if (!ctx.config_verify_artifact_path.empty()) {
      std::cerr << "info: config verify artifact: " << ctx.config_verify_artifact_path.string()
                << '\n';
    }
    if (!ctx.camera_config_artifact_path.empty()) {
      std::cerr << "info: camera config artifact: " << ctx.camera_config_artifact_path.string()
                << '\n';
    }
    if (!ctx.config_report_artifact_path.empty()) {
      std::cerr << "info: config report artifact: " << ctx.config_report_artifact_path.string()
                << '\n';
    }
    if (!ctx.sdk_log_artifact_path.empty() && fs::exists(ctx.sdk_log_artifact_path)) {
      std::cerr << "info: sdk log artifact: " << ctx.sdk_log_artifact_path.string() << '\n';
    }
    if (mapped_connect_error.has_value()) {
      std::cerr << "error: backend connect failed: " << mapped_connect_error->formatted_message
                << '\n';
    } else {
      std::cerr << "error: backend connect failed: " << ctx.error << '\n';
    }
    return kExitBackendConnectFailed;
  }
  ctx.logger.Info("backend connected", {{"backend", ctx.run_info.config.backend}});

  if (ctx.run_plan.backend == kBackendSim) {
    if (!backends::sim::ApplyScenarioConfig(*ctx.backend, ctx.run_plan.sim_config, ctx.error,
                                            &ctx.applied_params)) {
      ctx.logger.Error("backend config apply failed", {{"error", ctx.error}});
      std::cerr << "error: backend config failed: " << ctx.error << '\n';
      return kExitFailure;
    }
  }
  ctx.logger.Debug("backend config applied",
                   {{"param_count", std::to_string(ctx.applied_params.size())}});

  if (!ctx.config_applied_event_emitted) {
    const auto config_applied_at = std::chrono::system_clock::now();
    if (!emitter.EmitConfigStatus(
            {
                .kind = events::Emitter::ConfigStatusEvent::Kind::kApplied,
                .ts = config_applied_at,
                .run_id = ctx.run_info.run_id,
                .scenario_id = ctx.run_info.config.scenario_id,
                .applied_params = ctx.applied_params,
            },
            ctx.error)) {
      ctx.logger.Error("failed to append CONFIG_APPLIED event", {{"error", ctx.error}});
      std::cerr << "error: failed to append CONFIG_APPLIED event: " << ctx.error << '\n';
      return kExitFailure;
    }
  }

  ctx.netem_teardown_guard = std::make_unique<ScopedNetemTeardown>(&ctx.logger);
  if (!ApplyNetemIfRequested(options, ctx.netem_suggestions, *ctx.netem_teardown_guard,
                             ctx.error)) {
    ctx.logger.Error("netem apply failed", {{"error", ctx.error}});
    std::cerr << "error: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!ctx.backend->Start(ctx.error)) {
    if (ctx.run_plan.backend == kBackendRealStub) {
      const RealFailureDetails mapped_start_error = MapRealFailure("start", ctx.error);
      ctx.logger.Error("backend start failed",
                       {{"error_code", mapped_start_error.code},
                        {"error_action", mapped_start_error.actionable_message},
                        {"error", ctx.error}});
      std::cerr << "error: backend start failed: " << mapped_start_error.formatted_message << '\n';
    } else {
      ctx.logger.Error("backend start failed", {{"error", ctx.error}});
      std::cerr << "error: backend start failed: " << ctx.error << '\n';
    }
    return kExitFailure;
  }
  ctx.logger.Info("stream started",
                  {{"fps", std::to_string(ctx.run_plan.sim_config.fps)},
                   {"duration_ms", std::to_string(ctx.run_plan.duration.count())}});
  ctx.stream_started = true;

  const auto started_at = std::chrono::system_clock::now();
  if (!ctx.is_resume) {
    ctx.run_info.timestamps.started_at = started_at;
  }

  if (!emitter.EmitStreamStarted(
          {
              .ts = started_at,
              .run_id = ctx.run_info.run_id,
              .scenario_id = ctx.run_info.config.scenario_id,
              .backend = ctx.run_info.config.backend,
              .duration_ms = static_cast<std::uint64_t>(ctx.run_plan.duration.count()),
              .fps = ctx.run_plan.sim_config.fps,
              .seed = ctx.run_plan.sim_config.seed,
              .soak_mode = options.soak_mode,
              .resume = ctx.is_resume,
          },
          ctx.error)) {
    ctx.logger.Error("failed to append STREAM_STARTED event", {{"error", ctx.error}});
    StopBackendIfStreamStarted(ctx);
    std::cerr << "error: failed to append STREAM_STARTED event: " << ctx.error << '\n';
    return kExitFailure;
  }

  for (const auto& frame : ctx.frames) {
    const bool dropped = frame.dropped.has_value() && frame.dropped.value();
    if (!ctx.latest_frame_ts.has_value() || frame.timestamp > ctx.latest_frame_ts.value()) {
      ctx.latest_frame_ts = frame.timestamp;
    }
    if (dropped) {
      ++ctx.dropped_count;
    } else {
      ++ctx.received_count;
    }
  }
  return kExitSuccess;
}

int ExecuteStreaming(const RunOptions& options, std::string_view success_prefix,
                     ScenarioRunResult* run_result, RunExecutionContext& ctx) {
  events::Emitter emitter(ctx.bundle_dir, ctx.events_path);

  auto append_frame_event = [&](const backends::FrameSample& frame) {
    const bool dropped = frame.dropped.has_value() && frame.dropped.value();
    events::Emitter::FrameOutcomeKind outcome_kind = events::Emitter::FrameOutcomeKind::kReceived;
    std::string drop_reason;
    switch (frame.outcome) {
    case backends::FrameOutcome::kDropped:
      outcome_kind = events::Emitter::FrameOutcomeKind::kDropped;
      drop_reason = "sim_fault_injection";
      break;
    case backends::FrameOutcome::kTimeout:
      outcome_kind = events::Emitter::FrameOutcomeKind::kTimeout;
      drop_reason = "acquisition_timeout";
      break;
    case backends::FrameOutcome::kIncomplete:
      outcome_kind = events::Emitter::FrameOutcomeKind::kIncomplete;
      drop_reason = "incomplete_frame";
      break;
    case backends::FrameOutcome::kReceived:
    default:
      outcome_kind = events::Emitter::FrameOutcomeKind::kReceived;
      break;
    }

    if (!ctx.latest_frame_ts.has_value() || frame.timestamp > ctx.latest_frame_ts.value()) {
      ctx.latest_frame_ts = frame.timestamp;
    }
    if (dropped) {
      ++ctx.dropped_count;
    } else {
      ++ctx.received_count;
    }

    return emitter.EmitFrameOutcome(
        {
            .ts = frame.timestamp,
            .outcome = outcome_kind,
            .run_id = ctx.run_info.run_id,
            .frame_id = frame.frame_id,
            .size_bytes = frame.size_bytes,
            .dropped = dropped,
            .reason = drop_reason.empty() ? std::nullopt : std::optional<std::string>(drop_reason),
        },
        ctx.error);
  };

  ScopedInterruptSignalHandler scoped_signal_handler;
  ctx.non_soak_completed_duration = ctx.run_plan.duration;
  if (!options.soak_mode) {
    if (ctx.run_plan.backend != kBackendRealStub) {
      const std::vector<backends::FrameSample> pulled_frames =
          ctx.backend->PullFrames(ctx.run_plan.duration, ctx.error);
      if (!ctx.error.empty()) {
        ctx.logger.Error("backend pull_frames failed", {{"error", ctx.error}});
        StopBackendIfStreamStarted(ctx);
        std::cerr << "error: backend pull_frames failed: " << ctx.error << '\n';
        return kExitFailure;
      }

      for (const auto& frame : pulled_frames) {
        if (!append_frame_event(frame)) {
          ctx.logger.Error("failed to append frame event", {{"error", ctx.error}});
          StopBackendIfStreamStarted(ctx);
          std::cerr << "error: failed to append frame event: " << ctx.error << '\n';
          return kExitFailure;
        }
        ctx.frames.push_back(frame);
      }
    } else {
      constexpr std::chrono::milliseconds kInterruptPollInterval(250);
      ctx.non_soak_completed_duration = std::chrono::milliseconds::zero();
      std::chrono::milliseconds remaining_duration = ctx.run_plan.duration;
      while (remaining_duration > std::chrono::milliseconds::zero()) {
        if (g_run_interrupt_requested.load()) {
          ctx.interrupted_by_signal = true;
          break;
        }

        const std::chrono::milliseconds chunk_duration =
            std::min(kInterruptPollInterval, remaining_duration);
        const std::vector<backends::FrameSample> pulled_frames =
            ctx.backend->PullFrames(chunk_duration, ctx.error);
        if (!ctx.error.empty()) {
          if (backends::real_sdk::IsLikelyDisconnectError(ctx.error)) {
            const std::string disconnect_error = ctx.error;
            const std::uint32_t reconnect_attempts_remaining =
                backends::real_sdk::ComputeReconnectAttemptsRemaining(kReconnectRetryLimit,
                                                                      ctx.reconnect_attempts_used);
            ctx.logger.Warn(
                "device disconnected during stream",
                {{"error", disconnect_error},
                 {"reconnect_attempts_used_total", std::to_string(ctx.reconnect_attempts_used)},
                 {"reconnect_attempts_remaining", std::to_string(reconnect_attempts_remaining)},
                 {"reconnect_retry_limit", std::to_string(kReconnectRetryLimit)}});
            if (!AppendTraceEvent(
                    events::EventType::kDeviceDisconnected, std::chrono::system_clock::now(),
                    {
                        {"run_id", ctx.run_info.run_id},
                        {"scenario_id", ctx.run_info.config.scenario_id},
                        {"error", disconnect_error},
                        {"reconnect_attempts_used_total",
                         std::to_string(ctx.reconnect_attempts_used)},
                        {"reconnect_attempts_remaining",
                         std::to_string(reconnect_attempts_remaining)},
                        {"reconnect_retry_limit", std::to_string(kReconnectRetryLimit)},
                    },
                    ctx.bundle_dir, ctx.events_path, ctx.error)) {
              ctx.logger.Error("failed to append DEVICE_DISCONNECTED event",
                               {{"error", ctx.error}});
              StopBackendIfStreamStarted(ctx);
              std::cerr << "error: failed to append DEVICE_DISCONNECTED event: " << ctx.error
                        << '\n';
              return kExitFailure;
            }

            if (reconnect_attempts_remaining == 0U) {
              ctx.disconnect_failure = true;
              ctx.disconnect_failure_error =
                  "device disconnect detected but reconnect budget is exhausted";
              break;
            }

            ctx.error.clear();
            const backends::real_sdk::ReconnectAttemptResult reconnect_result =
                backends::real_sdk::ExecuteReconnectAttempts(
                    *ctx.backend, reconnect_attempts_remaining, ctx.reconnect_attempts_used,
                    ctx.logger);
            ctx.reconnect_attempts_used = reconnect_result.attempts_used_total;
            if (reconnect_result.reconnected) {
              continue;
            }

            ctx.disconnect_failure = true;
            ctx.disconnect_failure_error =
                reconnect_result.error.empty() ? disconnect_error : reconnect_result.error;
            ctx.logger.Error(
                "reconnect attempts exhausted after disconnect",
                {{"disconnect_error", disconnect_error},
                 {"reconnect_error", ctx.disconnect_failure_error},
                 {"reconnect_attempts_used_total", std::to_string(ctx.reconnect_attempts_used)},
                 {"reconnect_retry_limit", std::to_string(kReconnectRetryLimit)}});
            break;
          }

          const RealFailureDetails mapped_pull_error = MapRealFailure("pull_frames", ctx.error);
          ctx.logger.Error("backend pull_frames failed",
                           {{"error_code", mapped_pull_error.code},
                            {"error_action", mapped_pull_error.actionable_message},
                            {"error", ctx.error}});
          StopBackendIfStreamStarted(ctx);
          std::cerr << "error: backend pull_frames failed: " << mapped_pull_error.formatted_message
                    << '\n';
          return kExitFailure;
        }

        for (const auto& frame : pulled_frames) {
          if (!append_frame_event(frame)) {
            ctx.logger.Error("failed to append frame event", {{"error", ctx.error}});
            StopBackendIfStreamStarted(ctx);
            std::cerr << "error: failed to append frame event: " << ctx.error << '\n';
            return kExitFailure;
          }
          ctx.frames.push_back(frame);
        }

        ctx.non_soak_completed_duration += chunk_duration;
        if (ctx.non_soak_completed_duration > ctx.run_plan.duration) {
          ctx.non_soak_completed_duration = ctx.run_plan.duration;
        }
        remaining_duration = ctx.run_plan.duration - ctx.non_soak_completed_duration;
      }

      if (ctx.interrupted_by_signal) {
        ctx.logger.Warn(
            "interrupt received; finalizing run with partial duration",
            {{"completed_duration_ms", std::to_string(ctx.non_soak_completed_duration.count())},
             {"requested_duration_ms", std::to_string(ctx.run_plan.duration.count())}});
      } else if (ctx.disconnect_failure) {
        ctx.logger.Warn(
            "device disconnect handling exhausted retries; finalizing partial run",
            {{"completed_duration_ms", std::to_string(ctx.non_soak_completed_duration.count())},
             {"requested_duration_ms", std::to_string(ctx.run_plan.duration.count())},
             {"reconnect_attempts_used_total", std::to_string(ctx.reconnect_attempts_used)},
             {"reconnect_retry_limit", std::to_string(kReconnectRetryLimit)}});
      }
    }
  } else {
    if (ctx.soak_frame_cache_path.empty()) {
      ctx.soak_frame_cache_path = ctx.bundle_dir / "soak_frames.jsonl";
    }
    if (ctx.completed_duration > ctx.run_plan.duration) {
      ctx.logger.Error("resume checkpoint has invalid completed duration");
      StopBackendIfStreamStarted(ctx);
      std::cerr << "error: checkpoint completed duration exceeds total run duration\n";
      return kExitFailure;
    }

    std::chrono::milliseconds remaining_duration = ctx.run_plan.duration - ctx.completed_duration;
    soak::CheckpointState checkpoint_state;
    checkpoint_state.run_id = ctx.run_info.run_id;
    checkpoint_state.scenario_path = fs::path(options.scenario_path);
    checkpoint_state.bundle_dir = ctx.bundle_dir;
    checkpoint_state.frame_cache_path = ctx.soak_frame_cache_path;
    checkpoint_state.total_duration = ctx.run_plan.duration;
    checkpoint_state.completed_duration = ctx.completed_duration;
    checkpoint_state.checkpoints_written =
        ctx.is_resume ? ctx.resume_checkpoint.checkpoints_written : 0U;
    checkpoint_state.frames_total = static_cast<std::uint64_t>(ctx.frames.size());
    checkpoint_state.frames_received = ctx.received_count;
    checkpoint_state.frames_dropped = ctx.dropped_count;
    checkpoint_state.timestamps = ctx.run_info.timestamps;
    checkpoint_state.updated_at = std::chrono::system_clock::now();
    checkpoint_state.status = soak::CheckpointStatus::kRunning;
    checkpoint_state.stop_reason.clear();

    while (remaining_duration > std::chrono::milliseconds::zero()) {
      const std::chrono::milliseconds chunk_duration =
          std::min(options.checkpoint_interval, remaining_duration);
      std::vector<backends::FrameSample> chunk_frames =
          ctx.backend->PullFrames(chunk_duration, ctx.error);
      if (!ctx.error.empty()) {
        if (ctx.run_plan.backend == kBackendRealStub) {
          const RealFailureDetails mapped_pull_error = MapRealFailure("pull_frames", ctx.error);
          ctx.logger.Error("backend pull_frames failed",
                           {{"error_code", mapped_pull_error.code},
                            {"error_action", mapped_pull_error.actionable_message},
                            {"error", ctx.error}});
          StopBackendIfStreamStarted(ctx);
          std::cerr << "error: backend pull_frames failed: " << mapped_pull_error.formatted_message
                    << '\n';
          return kExitFailure;
        }
        ctx.logger.Error("backend pull_frames failed", {{"error", ctx.error}});
        StopBackendIfStreamStarted(ctx);
        std::cerr << "error: backend pull_frames failed: " << ctx.error << '\n';
        return kExitFailure;
      }

      const std::uint64_t frame_id_offset =
          ctx.frames.empty() ? 0U : (ctx.frames.back().frame_id + 1U);
      std::vector<backends::FrameSample> normalized_chunk;
      normalized_chunk.reserve(chunk_frames.size());
      for (auto frame : chunk_frames) {
        frame.frame_id += frame_id_offset;
        if (ctx.latest_frame_ts.has_value() && frame.timestamp <= ctx.latest_frame_ts.value()) {
          frame.timestamp = ctx.latest_frame_ts.value() + std::chrono::microseconds(1);
        }

        if (!append_frame_event(frame)) {
          ctx.logger.Error("failed to append frame event", {{"error", ctx.error}});
          StopBackendIfStreamStarted(ctx);
          std::cerr << "error: failed to append frame event: " << ctx.error << '\n';
          return kExitFailure;
        }

        ctx.frames.push_back(frame);
        normalized_chunk.push_back(frame);
      }

      if (!normalized_chunk.empty() &&
          !soak::AppendFrameCache(normalized_chunk, ctx.soak_frame_cache_path, ctx.error)) {
        ctx.logger.Error("failed to append soak frame cache", {{"error", ctx.error}});
        StopBackendIfStreamStarted(ctx);
        std::cerr << "error: failed to append soak frame cache: " << ctx.error << '\n';
        return kExitFailure;
      }

      ctx.completed_duration += chunk_duration;
      if (ctx.completed_duration > ctx.run_plan.duration) {
        ctx.completed_duration = ctx.run_plan.duration;
      }
      remaining_duration = ctx.run_plan.duration - ctx.completed_duration;

      checkpoint_state.completed_duration = ctx.completed_duration;
      checkpoint_state.frames_total = static_cast<std::uint64_t>(ctx.frames.size());
      checkpoint_state.frames_received = ctx.received_count;
      checkpoint_state.frames_dropped = ctx.dropped_count;
      checkpoint_state.updated_at = std::chrono::system_clock::now();
      checkpoint_state.status = soak::CheckpointStatus::kRunning;
      checkpoint_state.stop_reason.clear();
      ++checkpoint_state.checkpoints_written;
      if (!soak::WriteCheckpointArtifacts(checkpoint_state, ctx.soak_checkpoint_latest_path,
                                          ctx.soak_checkpoint_history_path, ctx.error)) {
        ctx.logger.Error("failed to write soak checkpoint", {{"error", ctx.error}});
        StopBackendIfStreamStarted(ctx);
        std::cerr << "error: failed to write soak checkpoint: " << ctx.error << '\n';
        return kExitFailure;
      }

      if (!AppendTraceEvent(
              events::EventType::kInfo, checkpoint_state.updated_at,
              {
                  {"run_id", ctx.run_info.run_id},
                  {"kind", "SOAK_CHECKPOINT"},
                  {"checkpoint_index", std::to_string(checkpoint_state.checkpoints_written)},
                  {"completed_duration_ms", std::to_string(ctx.completed_duration.count())},
                  {"remaining_duration_ms", std::to_string(remaining_duration.count())},
              },
              ctx.bundle_dir, ctx.events_path, ctx.error)) {
        ctx.logger.Error("failed to append SOAK_CHECKPOINT event", {{"error", ctx.error}});
        StopBackendIfStreamStarted(ctx);
        std::cerr << "error: failed to append SOAK_CHECKPOINT event: " << ctx.error << '\n';
        return kExitFailure;
      }

      const std::string stop_reason = ResolveSoakStopReason(options);
      if (!stop_reason.empty() && remaining_duration > std::chrono::milliseconds::zero()) {
        checkpoint_state.status = soak::CheckpointStatus::kPaused;
        checkpoint_state.stop_reason = stop_reason;
        checkpoint_state.timestamps.finished_at = std::chrono::system_clock::now();
        checkpoint_state.updated_at = checkpoint_state.timestamps.finished_at;
        ctx.run_info.timestamps.finished_at = checkpoint_state.timestamps.finished_at;
        if (ctx.latest_frame_ts.has_value() &&
            ctx.run_info.timestamps.finished_at < ctx.latest_frame_ts.value()) {
          ctx.run_info.timestamps.finished_at = ctx.latest_frame_ts.value();
          checkpoint_state.timestamps.finished_at = ctx.latest_frame_ts.value();
          checkpoint_state.updated_at = ctx.latest_frame_ts.value();
        }
        if (!soak::WriteCheckpointArtifacts(checkpoint_state, ctx.soak_checkpoint_latest_path,
                                            ctx.soak_checkpoint_history_path, ctx.error)) {
          ctx.logger.Error("failed to persist paused soak checkpoint", {{"error", ctx.error}});
          StopBackendIfStreamStarted(ctx);
          std::cerr << "error: failed to persist paused soak checkpoint: " << ctx.error << '\n';
          return kExitFailure;
        }

        StopBackendIfStreamStarted(ctx);
        if (!AppendTraceEvent(
                events::EventType::kStreamStopped, ctx.run_info.timestamps.finished_at,
                {
                    {"run_id", ctx.run_info.run_id},
                    {"frames_total", std::to_string(ctx.frames.size())},
                    {"frames_received", std::to_string(ctx.received_count)},
                    {"frames_dropped", std::to_string(ctx.dropped_count)},
                    {"reason", "soak_paused"},
                    {"completed_duration_ms", std::to_string(ctx.completed_duration.count())},
                    {"remaining_duration_ms", std::to_string(remaining_duration.count())},
                },
                ctx.bundle_dir, ctx.events_path, ctx.error)) {
          ctx.logger.Error("failed to append STREAM_STOPPED pause event", {{"error", ctx.error}});
          std::cerr << "error: failed to append STREAM_STOPPED pause event: " << ctx.error << '\n';
          return kExitFailure;
        }

        if (ctx.run_plan.backend == kBackendRealStub) {
          AttachTransportCountersToRunInfo(ctx.backend->DumpConfig(), ctx.run_info);
        }
        if (!artifacts::WriteRunJson(ctx.run_info, ctx.bundle_dir, ctx.run_artifact_path,
                                     ctx.error)) {
          ctx.logger.Error("failed to write run.json during soak pause", {{"error", ctx.error}});
          std::cerr << "error: failed to write run.json during soak pause: " << ctx.error << '\n';
          return kExitFailure;
        }
        if (run_result != nullptr) {
          run_result->run_json_path = ctx.run_artifact_path;
          run_result->events_jsonl_path = ctx.events_path;
        }

        artifacts::BundleArtifactRegistry bundle_registry;
        bundle_registry.RegisterMany({
            ctx.scenario_artifact_path,
            ctx.hostprobe_artifact_path,
            ctx.run_artifact_path,
            ctx.events_path,
            ctx.soak_checkpoint_latest_path,
            ctx.soak_checkpoint_history_path,
            ctx.soak_frame_cache_path,
        });
        bundle_registry.RegisterMany(ctx.hostprobe_raw_artifact_paths);
        bundle_registry.RegisterOptional(ctx.sdk_log_artifact_path);
        bundle_registry.RegisterOptional(ctx.config_verify_artifact_path);
        bundle_registry.RegisterOptional(ctx.camera_config_artifact_path);
        bundle_registry.RegisterOptional(ctx.config_report_artifact_path);
        const std::vector<fs::path> bundle_artifact_paths = bundle_registry.BuildManifestInput();
        if (!artifacts::WriteBundleManifestJson(ctx.bundle_dir, bundle_artifact_paths,
                                                ctx.bundle_manifest_path, ctx.error)) {
          ctx.logger.Error("failed to write bundle manifest during soak pause",
                           {{"error", ctx.error}});
          std::cerr << "error: failed to write bundle manifest during soak pause: " << ctx.error
                    << '\n';
          return kExitFailure;
        }

        ctx.logger.Info("soak run paused safely",
                        {{"run_id", ctx.run_info.run_id},
                         {"bundle_dir", ctx.bundle_dir.string()},
                         {"checkpoint", ctx.soak_checkpoint_latest_path.string()},
                         {"reason", stop_reason}});

        std::cout << success_prefix << options.scenario_path << '\n';
        std::cout << "run_id: " << ctx.run_info.run_id << '\n';
        std::cout << "bundle: " << ctx.bundle_dir.string() << '\n';
        std::cout << "events: " << ctx.events_path.string() << '\n';
        if (!ctx.config_verify_artifact_path.empty()) {
          std::cout << "config_verify: " << ctx.config_verify_artifact_path.string() << '\n';
        }
        if (!ctx.camera_config_artifact_path.empty()) {
          std::cout << "camera_config: " << ctx.camera_config_artifact_path.string() << '\n';
        }
        if (!ctx.config_report_artifact_path.empty()) {
          std::cout << "config_report: " << ctx.config_report_artifact_path.string() << '\n';
        }
        if (!ctx.sdk_log_artifact_path.empty() && fs::exists(ctx.sdk_log_artifact_path)) {
          std::cout << "sdk_log: " << ctx.sdk_log_artifact_path.string() << '\n';
        }
        std::cout << "artifact: " << ctx.run_artifact_path.string() << '\n';
        std::cout << "manifest: " << ctx.bundle_manifest_path.string() << '\n';
        std::cout << "soak_mode: enabled\n";
        std::cout << "soak_status: paused\n";
        std::cout << "soak_checkpoint: " << ctx.soak_checkpoint_latest_path.string() << '\n';
        std::cout << "soak_frame_cache: " << ctx.soak_frame_cache_path.string() << '\n';
        std::cout << "soak_completed_duration_ms: " << ctx.completed_duration.count() << '\n';
        std::cout << "soak_remaining_duration_ms: " << remaining_duration.count() << '\n';
        ctx.soak_paused = true;
        return kExitSuccess;
      }
    }

    checkpoint_state.status = soak::CheckpointStatus::kCompleted;
    checkpoint_state.stop_reason = "completed";
    checkpoint_state.completed_duration = ctx.run_plan.duration;
    checkpoint_state.frames_total = static_cast<std::uint64_t>(ctx.frames.size());
    checkpoint_state.frames_received = ctx.received_count;
    checkpoint_state.frames_dropped = ctx.dropped_count;
    checkpoint_state.timestamps.finished_at = std::chrono::system_clock::now();
    checkpoint_state.updated_at = checkpoint_state.timestamps.finished_at;
    ctx.run_info.timestamps.finished_at = checkpoint_state.timestamps.finished_at;
    if (ctx.latest_frame_ts.has_value() &&
        ctx.run_info.timestamps.finished_at < ctx.latest_frame_ts.value()) {
      ctx.run_info.timestamps.finished_at = ctx.latest_frame_ts.value();
      checkpoint_state.timestamps.finished_at = ctx.latest_frame_ts.value();
      checkpoint_state.updated_at = ctx.latest_frame_ts.value();
    }
    ++checkpoint_state.checkpoints_written;
    if (!soak::WriteCheckpointArtifacts(checkpoint_state, ctx.soak_checkpoint_latest_path,
                                        ctx.soak_checkpoint_history_path, ctx.error)) {
      ctx.logger.Error("failed to write final soak checkpoint", {{"error", ctx.error}});
      StopBackendIfStreamStarted(ctx);
      std::cerr << "error: failed to write final soak checkpoint: " << ctx.error << '\n';
      return kExitFailure;
    }
  }

  if (!ctx.backend->Stop(ctx.error)) {
    if (ctx.run_plan.backend == kBackendRealStub) {
      const RealFailureDetails mapped_stop_error = MapRealFailure("stop", ctx.error);
      ctx.logger.Error("backend stop failed",
                       {{"error_code", mapped_stop_error.code},
                        {"error_action", mapped_stop_error.actionable_message},
                        {"error", ctx.error}});
      std::cerr << "error: backend stop failed: " << mapped_stop_error.formatted_message << '\n';
    } else {
      ctx.logger.Error("backend stop failed", {{"error", ctx.error}});
      std::cerr << "error: backend stop failed: " << ctx.error << '\n';
    }
    return kExitFailure;
  }
  ctx.stream_started = false;

  if (!options.soak_mode) {
    auto finished_at = std::chrono::system_clock::now();
    if (ctx.latest_frame_ts.has_value() && finished_at < ctx.latest_frame_ts.value()) {
      finished_at = ctx.latest_frame_ts.value();
    }
    ctx.run_info.timestamps.finished_at = finished_at;
  }

  std::string stream_stop_reason = options.soak_mode ? "soak_completed" : "completed";
  if (!options.soak_mode && ctx.disconnect_failure) {
    stream_stop_reason = "device_disconnect";
  } else if (!options.soak_mode && ctx.interrupted_by_signal) {
    stream_stop_reason = "signal_interrupt";
  }
  std::map<std::string, std::string> stream_stopped_payload = {
      {"run_id", ctx.run_info.run_id},
      {"frames_total", std::to_string(ctx.frames.size())},
      {"frames_received", std::to_string(ctx.received_count)},
      {"frames_dropped", std::to_string(ctx.dropped_count)},
      {"reason", stream_stop_reason},
  };
  if (!options.soak_mode && (ctx.interrupted_by_signal || ctx.disconnect_failure)) {
    stream_stopped_payload["requested_duration_ms"] = std::to_string(ctx.run_plan.duration.count());
    stream_stopped_payload["completed_duration_ms"] =
        std::to_string(ctx.non_soak_completed_duration.count());
  }
  if (!options.soak_mode && ctx.disconnect_failure) {
    stream_stopped_payload["reconnect_attempts_used_total"] =
        std::to_string(ctx.reconnect_attempts_used);
    stream_stopped_payload["reconnect_retry_limit"] = std::to_string(kReconnectRetryLimit);
    if (!ctx.disconnect_failure_error.empty()) {
      stream_stopped_payload["disconnect_error"] = ctx.disconnect_failure_error;
    }
  }

  if (!AppendTraceEvent(events::EventType::kStreamStopped, ctx.run_info.timestamps.finished_at,
                        std::move(stream_stopped_payload), ctx.bundle_dir, ctx.events_path,
                        ctx.error)) {
    ctx.logger.Error("failed to append STREAM_STOPPED event", {{"error", ctx.error}});
    std::cerr << "error: failed to append STREAM_STOPPED event: " << ctx.error << '\n';
    return kExitFailure;
  }

  return kExitSuccess;
}

int FinalizeMetricsAndReports(const RunOptions& options, ScenarioRunResult* run_result,
                              RunExecutionContext& ctx) {
  events::Emitter emitter(ctx.bundle_dir, ctx.events_path);

  if (ctx.run_plan.backend == kBackendRealStub) {
    AttachTransportCountersToRunInfo(ctx.backend->DumpConfig(), ctx.run_info);
  }
  if (!artifacts::WriteRunJson(ctx.run_info, ctx.bundle_dir, ctx.run_artifact_path, ctx.error)) {
    ctx.logger.Error("failed to write run.json", {{"error", ctx.error}});
    std::cerr << "error: " << ctx.error << '\n';
    return kExitFailure;
  }
  if (run_result != nullptr) {
    run_result->run_json_path = ctx.run_artifact_path;
    run_result->events_jsonl_path = ctx.events_path;
  }

  std::chrono::milliseconds metrics_duration = ctx.run_plan.duration;
  if (!options.soak_mode && (ctx.interrupted_by_signal || ctx.disconnect_failure)) {
    metrics_duration = ctx.non_soak_completed_duration;
    if (metrics_duration <= std::chrono::milliseconds::zero()) {
      metrics_duration = std::chrono::milliseconds(1);
    }
  }

  if (!metrics::ComputeFpsReport(ctx.frames, metrics_duration, std::chrono::milliseconds(1'000),
                                 ctx.fps_report, ctx.error)) {
    ctx.logger.Error("failed to compute metrics", {{"error", ctx.error}});
    std::cerr << "error: failed to compute fps metrics: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!artifacts::WriteMetricsCsv(ctx.fps_report, ctx.bundle_dir, ctx.metrics_csv_path,
                                  ctx.error)) {
    ctx.logger.Error("failed to write metrics.csv", {{"error", ctx.error}});
    std::cerr << "error: failed to write metrics.csv: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!artifacts::WriteMetricsJson(ctx.fps_report, ctx.bundle_dir, ctx.metrics_json_path,
                                   ctx.error)) {
    ctx.logger.Error("failed to write metrics.json", {{"error", ctx.error}});
    std::cerr << "error: failed to write metrics.json: " << ctx.error << '\n';
    return kExitFailure;
  }
  if (run_result != nullptr) {
    run_result->metrics_json_path = ctx.metrics_json_path;
  }

  ctx.thresholds_passed = true;
  if (ctx.interrupted_by_signal) {
    ctx.thresholds_passed = false;
    ctx.threshold_failures.push_back(
        "run interrupted by signal before requested duration completed");
  } else if (ctx.disconnect_failure) {
    ctx.thresholds_passed = false;
    std::string disconnect_failure_text =
        "device disconnected mid-run and reconnect attempts were exhausted";
    if (!ctx.disconnect_failure_error.empty()) {
      disconnect_failure_text += ": " + ctx.disconnect_failure_error;
    }
    ctx.threshold_failures.push_back(disconnect_failure_text);
  } else {
    ctx.thresholds_passed =
        EvaluateRunThresholds(ctx.run_plan.thresholds, ctx.fps_report, ctx.threshold_failures);
  }
  if (run_result != nullptr) {
    run_result->thresholds_passed = ctx.thresholds_passed;
  }
  ctx.top_anomalies = metrics::BuildAnomalyHighlights(ctx.fps_report, ctx.run_plan.sim_config.fps,
                                                      ctx.threshold_failures);

  const std::vector<events::TransportAnomalyFinding> transport_anomalies =
      events::DetectTransportAnomalies(ctx.run_info);
  if (!transport_anomalies.empty()) {
    auto it = std::find(ctx.top_anomalies.begin(), ctx.top_anomalies.end(),
                        "No notable anomalies detected by current heuristics.");
    if (it != ctx.top_anomalies.end()) {
      ctx.top_anomalies.erase(it);
    }
  }
  for (const auto& anomaly : transport_anomalies) {
    ctx.top_anomalies.push_back(anomaly.summary);
    if (!emitter.EmitTransportAnomaly(
            {
                .ts = ctx.run_info.timestamps.finished_at,
                .run_id = ctx.run_info.run_id,
                .scenario_id = ctx.run_info.config.scenario_id,
                .heuristic_id = anomaly.heuristic_id,
                .counter = anomaly.counter_name,
                .observed_value = anomaly.observed_value,
                .threshold = anomaly.threshold,
                .summary = anomaly.summary,
            },
            ctx.error)) {
      ctx.logger.Error("failed to append TRANSPORT_ANOMALY event", {{"error", ctx.error}});
      std::cerr << "error: failed to append TRANSPORT_ANOMALY event: " << ctx.error << '\n';
      return kExitFailure;
    }
  }

  if (!artifacts::WriteRunSummaryMarkdown(ctx.run_info, ctx.fps_report, ctx.run_plan.sim_config.fps,
                                          ctx.thresholds_passed, ctx.threshold_failures,
                                          ctx.top_anomalies, ctx.netem_suggestions, ctx.bundle_dir,
                                          ctx.summary_markdown_path, ctx.error)) {
    ctx.logger.Error("failed to write summary.md", {{"error", ctx.error}});
    std::cerr << "error: failed to write summary.md: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (!artifacts::WriteRunSummaryHtml(ctx.run_info, ctx.fps_report, ctx.run_plan.sim_config.fps,
                                      ctx.thresholds_passed, ctx.threshold_failures,
                                      ctx.top_anomalies, ctx.bundle_dir, ctx.report_html_path,
                                      ctx.error)) {
    ctx.logger.Error("failed to write report.html", {{"error", ctx.error}});
    std::cerr << "error: failed to write report.html: " << ctx.error << '\n';
    return kExitFailure;
  }

  artifacts::BundleArtifactRegistry bundle_registry;
  bundle_registry.RegisterMany({
      ctx.scenario_artifact_path,
      ctx.hostprobe_artifact_path,
      ctx.run_artifact_path,
      ctx.events_path,
      ctx.metrics_csv_path,
      ctx.metrics_json_path,
      ctx.summary_markdown_path,
      ctx.report_html_path,
  });
  bundle_registry.RegisterMany(ctx.hostprobe_raw_artifact_paths);
  bundle_registry.RegisterOptional(ctx.sdk_log_artifact_path);
  bundle_registry.RegisterOptional(ctx.config_verify_artifact_path);
  bundle_registry.RegisterOptional(ctx.camera_config_artifact_path);
  bundle_registry.RegisterOptional(ctx.config_report_artifact_path);
  if (options.soak_mode) {
    bundle_registry.RegisterOptional(ctx.soak_frame_cache_path);
    bundle_registry.RegisterOptional(ctx.soak_checkpoint_latest_path);
    bundle_registry.RegisterOptional(ctx.soak_checkpoint_history_path);
  }
  const std::vector<fs::path> bundle_artifact_paths = bundle_registry.BuildManifestInput();
  if (!artifacts::WriteBundleManifestJson(ctx.bundle_dir, bundle_artifact_paths,
                                          ctx.bundle_manifest_path, ctx.error)) {
    ctx.logger.Error("failed to write bundle manifest", {{"error", ctx.error}});
    std::cerr << "error: failed to write bundle manifest: " << ctx.error << '\n';
    return kExitFailure;
  }

  if (options.zip_bundle) {
    if (!artifacts::WriteBundleZip(ctx.bundle_dir, ctx.bundle_zip_path, ctx.error)) {
      ctx.logger.Error("failed to write support bundle zip", {{"error", ctx.error}});
      std::cerr << "error: failed to write support bundle zip: " << ctx.error << '\n';
      return kExitFailure;
    }
  }

  ctx.logger.Info(
      "run artifacts written",
      {{"bundle_dir", ctx.bundle_dir.string()},
       {"events", ctx.events_path.string()},
       {"config_verify",
        ctx.config_verify_artifact_path.empty() ? "-" : ctx.config_verify_artifact_path.string()},
       {"camera_config",
        ctx.camera_config_artifact_path.empty() ? "-" : ctx.camera_config_artifact_path.string()},
       {"config_report",
        ctx.config_report_artifact_path.empty() ? "-" : ctx.config_report_artifact_path.string()},
       {"sdk_log", ctx.sdk_log_artifact_path.empty() ? "-" : ctx.sdk_log_artifact_path.string()},
       {"metrics_json", ctx.metrics_json_path.string()},
       {"summary", ctx.summary_markdown_path.string()},
       {"report_html", ctx.report_html_path.string()}});
  return kExitSuccess;
}

int EmitFinalConsoleSummary(const RunOptions& options, std::string_view success_prefix,
                            RunExecutionContext& ctx) {
  std::cout << success_prefix << options.scenario_path << '\n';
  std::cout << "run_id: " << ctx.run_info.run_id << '\n';
  if (ctx.run_info.real_device.has_value()) {
    const core::schema::RealDeviceMetadata& real = ctx.run_info.real_device.value();
    std::cout << "selected_device_type: real\n";
    std::cout << "selected_device_model: " << real.model << '\n';
    std::cout << "selected_device_serial: " << real.serial << '\n';
    std::cout << "selected_device_transport: " << real.transport << '\n';
  }
  if (ctx.run_info.webcam_device.has_value()) {
    const core::schema::WebcamDeviceMetadata& webcam = ctx.run_info.webcam_device.value();
    std::cout << "selected_device_type: webcam\n";
    std::cout << "selected_webcam_id: " << webcam.device_id << '\n';
    std::cout << "selected_webcam_name: " << webcam.friendly_name << '\n';
    if (webcam.selector_text.has_value()) {
      std::cout << "selected_webcam_selector: " << webcam.selector_text.value() << '\n';
    }
    if (webcam.selection_rule.has_value()) {
      std::cout << "selected_webcam_rule: " << webcam.selection_rule.value() << '\n';
    }
    if (webcam.discovered_index.has_value()) {
      std::cout << "selected_webcam_index: " << webcam.discovered_index.value() << '\n';
    }
  }
  std::cout << "bundle: " << ctx.bundle_dir.string() << '\n';
  std::cout << "scenario: " << ctx.scenario_artifact_path.string() << '\n';
  std::cout << "hostprobe: " << ctx.hostprobe_artifact_path.string() << '\n';
  std::cout << "hostprobe_raw_count: " << ctx.hostprobe_raw_artifact_paths.size() << '\n';
  std::cout << "redaction: " << (options.redact_identifiers ? "enabled" : "disabled") << '\n';
  std::string sdk_log_capture_status = "disabled";
  if (options.capture_sdk_log) {
    sdk_log_capture_status = ctx.sdk_log_artifact_path.empty() ? "ignored" : "enabled";
  }
  std::cout << "sdk_log_capture: " << sdk_log_capture_status << '\n';
  std::cout << "soak_mode: " << (options.soak_mode ? "enabled" : "disabled") << '\n';
  if (options.soak_mode) {
    std::cout << "soak_checkpoint_interval_ms: " << options.checkpoint_interval.count() << '\n';
    if (!ctx.soak_checkpoint_latest_path.empty()) {
      std::cout << "soak_checkpoint: " << ctx.soak_checkpoint_latest_path.string() << '\n';
    }
    if (!ctx.soak_frame_cache_path.empty()) {
      std::cout << "soak_frame_cache: " << ctx.soak_frame_cache_path.string() << '\n';
    }
  }
  std::cout << "netem_apply: " << (options.apply_netem ? "enabled" : "disabled");
  if (options.apply_netem) {
    std::cout << " iface=" << options.netem_interface;
    if (options.apply_netem_force) {
      std::cout << " force=true";
    }
  }
  std::cout << '\n';
  std::cout << "artifact: " << ctx.run_artifact_path.string() << '\n';
  std::cout << "events: " << ctx.events_path.string() << '\n';
  if (!ctx.config_verify_artifact_path.empty()) {
    std::cout << "config_verify: " << ctx.config_verify_artifact_path.string() << '\n';
  }
  if (!ctx.camera_config_artifact_path.empty()) {
    std::cout << "camera_config: " << ctx.camera_config_artifact_path.string() << '\n';
  }
  if (!ctx.config_report_artifact_path.empty()) {
    std::cout << "config_report: " << ctx.config_report_artifact_path.string() << '\n';
  }
  if (!ctx.sdk_log_artifact_path.empty() && fs::exists(ctx.sdk_log_artifact_path)) {
    std::cout << "sdk_log: " << ctx.sdk_log_artifact_path.string() << '\n';
  }
  std::cout << "metrics_csv: " << ctx.metrics_csv_path.string() << '\n';
  std::cout << "metrics_json: " << ctx.metrics_json_path.string() << '\n';
  std::cout << "summary: " << ctx.summary_markdown_path.string() << '\n';
  std::cout << "report_html: " << ctx.report_html_path.string() << '\n';
  std::cout << "manifest: " << ctx.bundle_manifest_path.string() << '\n';
  if (options.zip_bundle) {
    std::cout << "bundle_zip: " << ctx.bundle_zip_path.string() << '\n';
  }
  std::cout << "fps: avg=" << ctx.fps_report.avg_fps
            << " rolling_samples=" << ctx.fps_report.rolling_samples.size() << '\n';
  std::cout << "drops: total=" << ctx.fps_report.dropped_frames_total
            << " generic=" << ctx.fps_report.dropped_generic_frames_total
            << " timeout=" << ctx.fps_report.timeout_frames_total
            << " incomplete=" << ctx.fps_report.incomplete_frames_total
            << " rate_percent=" << ctx.fps_report.drop_rate_percent << '\n';
  std::cout << "timing_us: interval_avg=" << ctx.fps_report.inter_frame_interval_us.avg_us
            << " interval_p95=" << ctx.fps_report.inter_frame_interval_us.p95_us
            << " jitter_avg=" << ctx.fps_report.inter_frame_jitter_us.avg_us
            << " jitter_p95=" << ctx.fps_report.inter_frame_jitter_us.p95_us << '\n';
  std::cout << "frames: total=" << ctx.frames.size() << " received=" << ctx.received_count
            << " dropped=" << ctx.dropped_count << '\n';

  std::string run_status = "completed";
  if (ctx.disconnect_failure) {
    run_status = "failed_device_disconnect";
  } else if (ctx.interrupted_by_signal) {
    run_status = "interrupted";
  }
  std::cout << "run_status: " << run_status << '\n';
  if (!options.soak_mode && ctx.interrupted_by_signal) {
    std::cout << "completed_duration_ms: " << ctx.non_soak_completed_duration.count() << '\n';
    std::cout << "requested_duration_ms: " << ctx.run_plan.duration.count() << '\n';
  } else if (!options.soak_mode && ctx.disconnect_failure) {
    std::cout << "completed_duration_ms: " << ctx.non_soak_completed_duration.count() << '\n';
    std::cout << "requested_duration_ms: " << ctx.run_plan.duration.count() << '\n';
    std::cout << "reconnect_attempts_used_total: " << ctx.reconnect_attempts_used << '\n';
    std::cout << "reconnect_retry_limit: " << kReconnectRetryLimit << '\n';
  }

  if (ctx.interrupted_by_signal) {
    ctx.logger.Warn(
        "run interrupted by signal",
        {{"frames_total", std::to_string(ctx.frames.size())},
         {"frames_received", std::to_string(ctx.received_count)},
         {"frames_dropped", std::to_string(ctx.dropped_count)},
         {"completed_duration_ms", std::to_string(ctx.non_soak_completed_duration.count())},
         {"requested_duration_ms", std::to_string(ctx.run_plan.duration.count())}});
    std::cerr << "warning: run interrupted by Ctrl+C; finalized partial artifact bundle\n";
    return kExitFailure;
  }
  if (ctx.disconnect_failure) {
    ctx.logger.Error(
        "run failed after device disconnect and reconnect exhaustion",
        {{"frames_total", std::to_string(ctx.frames.size())},
         {"frames_received", std::to_string(ctx.received_count)},
         {"frames_dropped", std::to_string(ctx.dropped_count)},
         {"completed_duration_ms", std::to_string(ctx.non_soak_completed_duration.count())},
         {"requested_duration_ms", std::to_string(ctx.run_plan.duration.count())},
         {"reconnect_attempts_used_total", std::to_string(ctx.reconnect_attempts_used)},
         {"reconnect_retry_limit", std::to_string(kReconnectRetryLimit)},
         {"error", ctx.disconnect_failure_error.empty() ? "-" : ctx.disconnect_failure_error}});
    std::cerr << "error: run failed after device disconnect; reconnect attempts exhausted\n";
    if (!ctx.disconnect_failure_error.empty()) {
      std::cerr << "error: disconnect detail: " << ctx.disconnect_failure_error << '\n';
    }
    return kExitFailure;
  }

  if (ctx.thresholds_passed) {
    ctx.logger.Info("run completed", {{"thresholds", "pass"},
                                      {"frames_total", std::to_string(ctx.frames.size())},
                                      {"frames_received", std::to_string(ctx.received_count)},
                                      {"frames_dropped", std::to_string(ctx.dropped_count)}});
    std::cout << "thresholds: pass\n";
    return kExitSuccess;
  }

  ctx.logger.Warn("run completed with threshold failures",
                  {{"thresholds", "fail"},
                   {"failure_count", std::to_string(ctx.threshold_failures.size())},
                   {"frames_total", std::to_string(ctx.frames.size())},
                   {"frames_received", std::to_string(ctx.received_count)},
                   {"frames_dropped", std::to_string(ctx.dropped_count)}});
  std::cout << "thresholds: fail count=" << ctx.threshold_failures.size() << '\n';
  for (const auto& failure : ctx.threshold_failures) {
    ctx.logger.Warn("threshold failure", {{"detail", failure}});
    std::cerr << "threshold failed: " << failure << '\n';
  }
  return kExitThresholdsFailed;
}

// Centralized run execution keeps `run` and `baseline capture` behavior aligned
// so artifact contracts and metrics math never diverge between modes.
int ExecuteScenarioRunInternal(const RunOptions& options, bool use_per_run_bundle_dir,
                               bool allow_zip_bundle, std::string_view success_prefix,
                               ScenarioRunResult* run_result) {
  if (run_result != nullptr) {
    *run_result = ScenarioRunResult{};
  }

  RunExecutionContext ctx(options.log_level);

  // Stage 1: parse/validate run context and resolve scenario execution mode.
  int stage_exit_code =
      PrepareRunContext(options, use_per_run_bundle_dir, allow_zip_bundle, run_result, ctx);
  if (stage_exit_code != kExitSuccess) {
    return stage_exit_code;
  }

  // Stage 2: materialize early artifacts (scenario snapshot + host evidence).
  stage_exit_code = InitializeArtifacts(options, ctx);
  if (stage_exit_code != kExitSuccess) {
    return stage_exit_code;
  }

  // Stage 3: create/configure/connect backend and emit STREAM_STARTED.
  stage_exit_code = ConfigureBackend(options, run_result, ctx);
  if (stage_exit_code != kExitSuccess) {
    return stage_exit_code;
  }

  // Stage 4: run streaming loops (normal/soak/resume) and append STREAM_STOPPED.
  stage_exit_code = ExecuteStreaming(options, success_prefix, run_result, ctx);
  if (stage_exit_code != kExitSuccess) {
    return stage_exit_code;
  }
  if (ctx.soak_paused) {
    return kExitSuccess;
  }

  // Stage 5: compute metrics and write summary/report/bundle artifacts.
  stage_exit_code = FinalizeMetricsAndReports(options, run_result, ctx);
  if (stage_exit_code != kExitSuccess) {
    return stage_exit_code;
  }

  // Stage 6: print user-facing summary and resolve final exit code.
  return EmitFinalConsoleSummary(options, success_prefix, ctx);
}

int CommandRun(const std::vector<std::string_view>& args) {
  RunOptions options;
  std::string error;
  if (!ParseRunOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  return labops::cli::ExecuteScenarioRun(options,
                                         /*use_per_run_bundle_dir=*/true,
                                         /*allow_zip_bundle=*/true, "run queued: ", nullptr);
}

int CommandBaselineCapture(const std::vector<std::string_view>& args) {
  RunOptions options;
  std::string error;
  if (!ParseBaselineCaptureOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  return labops::cli::ExecuteScenarioRun(options,
                                         /*use_per_run_bundle_dir=*/false,
                                         /*allow_zip_bundle=*/false,
                                         "baseline captured: ", nullptr);
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

int CommandListDevices(const std::vector<std::string_view>& args) {
  ListDevicesOptions options;
  std::string error;
  if (!ParseListDevicesOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    PrintListDevicesUsage(std::cerr);
    return kExitUsage;
  }

  if (!backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    std::cerr << "error: BACKEND_NOT_AVAILABLE: real backend "
              << backends::real_sdk::RealBackendAvailabilityStatusText() << '\n';
    return kExitFailure;
  }

  std::vector<backends::real_sdk::DeviceInfo> devices;
  if (!backends::real_sdk::EnumerateConnectedDevices(devices, error)) {
    const RealFailureDetails mapped_discovery_error = MapRealFailure("device_discovery", error);
    std::cerr << "error: DEVICE_DISCOVERY_FAILED: " << mapped_discovery_error.formatted_message
              << '\n';
    return kExitFailure;
  }

  std::cout << "backend: real\n";
  std::cout << "status: enabled\n";
  std::cout << "devices: " << devices.size() << '\n';
  if (devices.empty()) {
    std::cout << "note: no cameras detected\n";
    std::cout << "hint: set LABOPS_REAL_DEVICE_FIXTURE to a descriptor CSV for local validation\n";
    return kExitSuccess;
  }

  for (std::size_t i = 0; i < devices.size(); ++i) {
    const backends::real_sdk::DeviceInfo& device = devices[i];
    std::cout << "device[" << i << "].model: " << device.model << '\n';
    std::cout << "device[" << i << "].serial: " << device.serial << '\n';
    std::cout << "device[" << i
              << "].user_id: " << (device.user_id.empty() ? "(none)" : device.user_id) << '\n';
    std::cout << "device[" << i << "].transport: " << device.transport << '\n';
    if (device.firmware_version.has_value()) {
      std::cout << "device[" << i << "].firmware_version: " << device.firmware_version.value()
                << '\n';
    }
    if (device.sdk_version.has_value()) {
      std::cout << "device[" << i << "].sdk_version: " << device.sdk_version.value() << '\n';
    }
    if (device.ip_address.has_value()) {
      std::cout << "device[" << i << "].ip: " << device.ip_address.value() << '\n';
    }
    if (device.mac_address.has_value()) {
      std::cout << "device[" << i << "].mac: " << device.mac_address.value() << '\n';
    }
  }
  return kExitSuccess;
}

int CommandKbDraft(const std::vector<std::string_view>& args) {
  KbDraftOptions options;
  std::string error;
  if (!ParseKbDraftOptions(args, options, error)) {
    std::cerr << "error: " << error << '\n';
    return kExitUsage;
  }

  fs::path written_path;
  if (!artifacts::WriteKbDraftFromRunFolder(options.run_folder, options.output_path, written_path,
                                            error)) {
    std::cerr << "error: failed to generate kb draft: " << error << '\n';
    return kExitFailure;
  }

  std::cout << "kb_draft: " << written_path.string() << '\n';
  std::cout << "source_run_folder: " << options.run_folder.string() << '\n';
  return kExitSuccess;
}

int CommandKb(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    std::cerr << "error: kb requires a subcommand\n";
    PrintKbUsage(std::cerr);
    return kExitUsage;
  }

  const std::string_view subcommand = args.front();
  const std::vector<std::string_view> sub_args(args.begin() + 1, args.end());
  if (subcommand == "draft") {
    return CommandKbDraft(sub_args);
  }

  std::cerr << "error: unknown kb subcommand: " << subcommand << '\n';
  PrintKbUsage(std::cerr);
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
  if (!artifacts::ComputeMetricsDiffFromCsv(baseline_metrics_csv_path, run_metrics_csv_path,
                                            diff_report, error)) {
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

int ExecuteScenarioRun(const RunOptions& options, bool use_per_run_bundle_dir,
                       bool allow_zip_bundle, std::string_view success_prefix,
                       ScenarioRunResult* run_result) {
  return ExecuteScenarioRunInternal(options, use_per_run_bundle_dir, allow_zip_bundle,
                                    success_prefix, run_result);
}

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

  if (command == "list-backends") {
    return CommandListBackends(args);
  }

  if (command == "list-devices") {
    return CommandListDevices(args);
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

  if (command == "kb") {
    return CommandKb(args);
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
