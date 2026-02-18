#include "labops/cli/router.hpp"

#include "artifacts/bundle_manifest_writer.hpp"
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
#include "backends/real_sdk/real_backend_factory.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "core/errors/exit_codes.hpp"
#include "core/json_dom.hpp"
#include "core/schema/run_contract.hpp"
#include "events/event_model.hpp"
#include "events/jsonl_writer.hpp"
#include "hostprobe/system_probe.hpp"
#include "labops/soak/checkpoint_store.hpp"
#include "metrics/anomalies.hpp"
#include "metrics/fps.hpp"
#include "scenarios/netem_profile_support.hpp"
#include "scenarios/validator.hpp"

#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
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
         "[--device <selector>] "
         "[--soak] [--checkpoint-interval-ms <ms>] [--resume <checkpoint.json>] "
         "[--soak-stop-file <path>] "
         "[--log-level <debug|info|warn|error>] "
         "[--apply-netem --netem-iface <iface> [--apply-netem-force]]\n"
      << "  labops baseline capture <scenario.json> [--redact] "
         "[--device <selector>] "
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
         "[--device <selector>] "
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
  backends::real_sdk::DeviceSelector parsed;
  if (backends::real_sdk::ParseDeviceSelector(selector_text, parsed, error)) {
    return true;
  }
  error = "invalid device selector '" + std::string(selector_text) + "': " + error;
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

// Run parsing keeps support for both canonical schema paths and historical
// flat fixture keys so old smoke tests and internal scripts continue to work.
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

bool TryGetNonNegativeInteger(const JsonValue& value, std::uint64_t& out) {
  if (value.type != JsonValue::Type::kNumber) {
    return false;
  }
  if (!std::isfinite(value.number_value) || value.number_value < 0.0) {
    return false;
  }
  const double floored = std::floor(value.number_value);
  if (floored != value.number_value ||
      floored > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return false;
  }
  out = static_cast<std::uint64_t>(floored);
  return true;
}

bool TryGetFiniteNumber(const JsonValue& value, double& out) {
  if (value.type != JsonValue::Type::kNumber || !std::isfinite(value.number_value)) {
    return false;
  }
  out = value.number_value;
  return true;
}

bool BuildRoiParamValue(const JsonValue& roi, std::string& roi_value, std::string& error) {
  roi_value.clear();
  error.clear();
  if (roi.type != JsonValue::Type::kObject) {
    error = "scenario camera.roi must include x, y, width, and height";
    return false;
  }

  auto read_required_non_negative_integer = [&](std::string_view key, std::uint64_t& parsed_value) {
    const JsonValue* value = FindObjectMember(roi, key);
    if (value == nullptr) {
      return false;
    }
    return TryGetNonNegativeInteger(*value, parsed_value);
  };

  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  if (!read_required_non_negative_integer("x", x) || !read_required_non_negative_integer("y", y) ||
      !read_required_non_negative_integer("width", width) ||
      !read_required_non_negative_integer("height", height)) {
    error = "scenario camera.roi must include x, y, width, and height";
    return false;
  }

  roi_value = "x=" + std::to_string(x) + ",y=" + std::to_string(y) +
              ",width=" + std::to_string(width) + ",height=" + std::to_string(height);
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
  std::string scenario_text;
  if (!ReadTextFile(scenario_path, scenario_text, error)) {
    return false;
  }

  JsonValue scenario_root;
  JsonParser parser(scenario_text);
  std::string parse_error;
  if (!parser.Parse(scenario_root, parse_error)) {
    error = "invalid scenario JSON: " + parse_error;
    return false;
  }
  if (scenario_root.type != JsonValue::Type::kObject) {
    error = "scenario root must be a JSON object";
    return false;
  }

  // Keep run-path behavior backwards compatible with old fixtures:
  // if a field is present but has an unexpected type, we treat it as unset.
  // `labops validate` remains the authoritative strict schema gate.
  auto read_u64 =
      [&](std::initializer_list<std::string_view> canonical_path,
          std::initializer_list<std::string_view> legacy_path) -> std::optional<std::uint64_t> {
    const JsonValue* value = FindScenarioField(scenario_root, canonical_path, legacy_path);
    if (value == nullptr) {
      return std::nullopt;
    }
    std::uint64_t parsed = 0;
    if (!TryGetNonNegativeInteger(*value, parsed)) {
      return std::nullopt;
    }
    return parsed;
  };

  auto read_number =
      [&](std::initializer_list<std::string_view> canonical_path,
          std::initializer_list<std::string_view> legacy_path) -> std::optional<double> {
    const JsonValue* value = FindScenarioField(scenario_root, canonical_path, legacy_path);
    if (value == nullptr) {
      return std::nullopt;
    }
    double parsed = 0.0;
    if (!TryGetFiniteNumber(*value, parsed)) {
      return std::nullopt;
    }
    return parsed;
  };

  auto read_string =
      [&](std::initializer_list<std::string_view> canonical_path,
          std::initializer_list<std::string_view> legacy_path) -> std::optional<std::string> {
    const JsonValue* value = FindScenarioField(scenario_root, canonical_path, legacy_path);
    if (value == nullptr || value->type != JsonValue::Type::kString) {
      return std::nullopt;
    }
    return value->string_value;
  };

  auto assign_u32 = [&](std::string_view key,
                        std::initializer_list<std::string_view> canonical_path,
                        std::initializer_list<std::string_view> legacy_path, std::uint32_t& target,
                        std::uint32_t max_value = std::numeric_limits<std::uint32_t>::max()) {
    const auto value = read_u64(canonical_path, legacy_path);
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

  auto assign_u64 = [&](std::initializer_list<std::string_view> canonical_path,
                        std::initializer_list<std::string_view> legacy_path,
                        std::uint64_t& target) {
    const auto value = read_u64(canonical_path, legacy_path);
    if (!value.has_value()) {
      return true;
    }
    target = value.value();
    return true;
  };

  auto assign_non_negative_double =
      [&](std::string_view key, std::initializer_list<std::string_view> canonical_path,
          std::initializer_list<std::string_view> legacy_path, std::optional<double>& target,
          bool percent_0_to_100 = false) {
        const auto value = read_number(canonical_path, legacy_path);
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

  auto assign_non_negative_integer_threshold =
      [&](std::string_view key, std::initializer_list<std::string_view> canonical_path,
          std::initializer_list<std::string_view> legacy_path,
          std::optional<std::uint64_t>& target) {
        const auto value = read_number(canonical_path, legacy_path);
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

  if (const auto duration_ms = read_u64({"duration", "duration_ms"}, {"duration_ms"});
      duration_ms.has_value()) {
    if (duration_ms.value() == 0U) {
      error = "scenario duration_ms must be greater than 0";
      return false;
    }
    plan.duration = std::chrono::milliseconds(static_cast<std::int64_t>(duration_ms.value()));
  } else if (const auto duration_s = read_u64({"duration", "duration_s"}, {"duration_s"});
             duration_s.has_value()) {
    if (duration_s.value() == 0U) {
      error = "scenario duration_s must be greater than 0";
      return false;
    }
    plan.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(static_cast<std::int64_t>(duration_s.value())));
  }

  if (const auto backend = read_string({"backend"}, {}); backend.has_value()) {
    if (*backend != kBackendSim && *backend != kBackendRealStub) {
      error = "scenario backend must be one of: sim, real_stub";
      return false;
    }
    plan.backend = *backend;
  }

  if (const auto apply_mode = read_string({"apply_mode"}, {}); apply_mode.has_value()) {
    if (!backends::real_sdk::ParseParamApplyMode(apply_mode.value(), plan.real_apply_mode, error)) {
      return false;
    }
  }

  const auto requested_fps = read_u64({"camera", "fps"}, {"fps"});
  const auto pixel_format = read_string({"camera", "pixel_format"}, {"pixel_format"});
  const auto exposure_us = read_u64({"camera", "exposure_us"}, {"exposure_us"});
  const auto gain_db = read_number({"camera", "gain_db"}, {"gain_db"});
  const auto trigger_mode = read_string({"camera", "trigger_mode"}, {"trigger_mode"});
  const auto trigger_source = read_string({"camera", "trigger_source"}, {"trigger_source"});
  std::string roi_value;
  const JsonValue* roi_object = FindScenarioField(scenario_root, {"camera", "roi"}, {"roi"});
  const bool has_roi = roi_object != nullptr;
  if (has_roi && !BuildRoiParamValue(*roi_object, roi_value, error)) {
    return false;
  }

  if (!assign_u32("fps", {"camera", "fps"}, {"fps"}, plan.sim_config.fps)) {
    return false;
  }
  if (!assign_u32("jitter_us", {"sim_faults", "jitter_us"}, {"jitter_us"},
                  plan.sim_config.jitter_us)) {
    return false;
  }
  if (!assign_u64({"sim_faults", "seed"}, {"seed"}, plan.sim_config.seed)) {
    return false;
  }
  if (!assign_u32("frame_size_bytes", {"camera", "frame_size_bytes"}, {"frame_size_bytes"},
                  plan.sim_config.frame_size_bytes)) {
    return false;
  }
  if (!assign_u32("drop_every_n", {"sim_faults", "drop_every_n"}, {"drop_every_n"},
                  plan.sim_config.drop_every_n)) {
    return false;
  }
  if (!assign_u32("drop_percent", {"sim_faults", "drop_percent"}, {"drop_percent"},
                  plan.sim_config.faults.drop_percent, 100U)) {
    return false;
  }
  if (!assign_u32("burst_drop", {"sim_faults", "burst_drop"}, {"burst_drop"},
                  plan.sim_config.faults.burst_drop)) {
    return false;
  }
  if (!assign_u32("reorder", {"sim_faults", "reorder"}, {"reorder"},
                  plan.sim_config.faults.reorder)) {
    return false;
  }

  if (requested_fps.has_value()) {
    UpsertRealParam(plan.real_params, "frame_rate", std::to_string(plan.sim_config.fps));
  }
  if (pixel_format.has_value() && !pixel_format->empty()) {
    UpsertRealParam(plan.real_params, "pixel_format", pixel_format.value());
  }
  if (exposure_us.has_value()) {
    UpsertRealParam(plan.real_params, "exposure", std::to_string(exposure_us.value()));
  }
  if (gain_db.has_value()) {
    UpsertRealParam(plan.real_params, "gain", FormatCompactDouble(gain_db.value()));
  }
  if (trigger_mode.has_value() && !trigger_mode->empty()) {
    UpsertRealParam(plan.real_params, "trigger_mode", trigger_mode.value());
  }
  if (trigger_source.has_value() && !trigger_source->empty()) {
    UpsertRealParam(plan.real_params, "trigger_source", trigger_source.value());
  }
  if (has_roi) {
    UpsertRealParam(plan.real_params, "roi", roi_value);
  }

  if (!assign_non_negative_double("min_avg_fps", {"thresholds", "min_avg_fps"}, {"min_avg_fps"},
                                  plan.thresholds.min_avg_fps)) {
    return false;
  }
  if (!assign_non_negative_double("max_drop_rate_percent", {"thresholds", "max_drop_rate_percent"},
                                  {"max_drop_rate_percent"}, plan.thresholds.max_drop_rate_percent,
                                  /*percent_0_to_100=*/true)) {
    return false;
  }
  if (!assign_non_negative_double(
          "max_inter_frame_interval_p95_us", {"thresholds", "max_inter_frame_interval_p95_us"},
          {"max_inter_frame_interval_p95_us"}, plan.thresholds.max_inter_frame_interval_p95_us)) {
    return false;
  }
  if (!assign_non_negative_double(
          "max_inter_frame_jitter_p95_us", {"thresholds", "max_inter_frame_jitter_p95_us"},
          {"max_inter_frame_jitter_p95_us"}, plan.thresholds.max_inter_frame_jitter_p95_us)) {
    return false;
  }
  if (!assign_non_negative_integer_threshold(
          "max_disconnect_count", {"thresholds", "max_disconnect_count"}, {"max_disconnect_count"},
          plan.thresholds.max_disconnect_count)) {
    return false;
  }

  if (const auto netem_profile = read_string({"netem_profile"}, {}); netem_profile.has_value()) {
    if (netem_profile->empty()) {
      error = "scenario netem_profile must not be empty";
      return false;
    }
    if (!scenarios::IsLowercaseSlug(netem_profile.value())) {
      error = "scenario netem_profile must use lowercase slug format [a-z0-9_-]+";
      return false;
    }
    plan.netem_profile = netem_profile.value();
  }

  if (const auto device_selector = read_string({"device_selector"}, {});
      device_selector.has_value()) {
    if (device_selector->empty()) {
      error = "scenario device_selector must not be empty";
      return false;
    }
    if (!ValidateDeviceSelectorText(device_selector.value(), error)) {
      return false;
    }
    plan.device_selector = device_selector.value();
  }

  if (plan.device_selector.has_value() && plan.backend != kBackendRealStub) {
    error = "device_selector requires backend real_stub";
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
  if (run_plan.backend == kBackendRealStub) {
    backend = backends::real_sdk::CreateRealBackend();
    return true;
  }

  error = "unsupported backend in run plan: " + run_plan.backend;
  return false;
}

struct ResolvedDeviceSelection {
  std::string selector_text;
  backends::real_sdk::DeviceInfo device;
  std::size_t discovered_index = 0;
};

void AttachResolvedDeviceMetadataToRunInfo(
    const std::optional<ResolvedDeviceSelection>& resolved_device_selection,
    core::schema::RunInfo& run_info) {
  run_info.real_device.reset();
  if (!resolved_device_selection.has_value()) {
    return;
  }

  const ResolvedDeviceSelection& selected = resolved_device_selection.value();
  core::schema::RealDeviceMetadata real_device;
  real_device.model = selected.device.model;
  real_device.serial = selected.device.serial;
  real_device.transport = selected.device.transport;
  if (!selected.device.user_id.empty()) {
    real_device.user_id = selected.device.user_id;
  }
  real_device.firmware_version = selected.device.firmware_version;
  real_device.sdk_version = selected.device.sdk_version.value_or("unknown");
  run_info.real_device = std::move(real_device);
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

  if (!selector_text.has_value()) {
    return true;
  }
  if (run_plan.backend != kBackendRealStub) {
    error = "--device/device_selector requires backend real_stub";
    return false;
  }

  backends::real_sdk::DeviceInfo selected_device;
  std::size_t selected_index = 0;
  if (!backends::real_sdk::ResolveConnectedDevice(selector_text.value(), selected_device,
                                                  selected_index, error)) {
    return false;
  }

  resolved = ResolvedDeviceSelection{
      .selector_text = selector_text.value(),
      .device = std::move(selected_device),
      .discovered_index = selected_index,
  };
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
      !apply("device.index", std::to_string(selection.discovered_index)) ||
      !apply("device.model", selection.device.model) ||
      !apply("device.serial", selection.device.serial) ||
      !apply("device.user_id",
             selection.device.user_id.empty() ? "(none)" : selection.device.user_id) ||
      !apply("device.transport", selection.device.transport)) {
    return false;
  }

  if (selection.device.ip_address.has_value() &&
      !apply("device.ip", selection.device.ip_address.value())) {
    return false;
  }
  if (selection.device.mac_address.has_value() &&
      !apply("device.mac", selection.device.mac_address.value())) {
    return false;
  }
  if (selection.device.firmware_version.has_value() &&
      !apply("device.firmware_version", selection.device.firmware_version.value())) {
    return false;
  }
  if (selection.device.sdk_version.has_value() &&
      !apply("device.sdk_version", selection.device.sdk_version.value())) {
    return false;
  }

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

std::map<std::string, std::string>
BuildConfigUnsupportedPayload(const core::schema::RunInfo& run_info,
                              const backends::real_sdk::UnsupportedParam& unsupported,
                              const backends::real_sdk::ParamApplyMode mode) {
  return {
      {"run_id", run_info.run_id},
      {"scenario_id", run_info.config.scenario_id},
      {"apply_mode", backends::real_sdk::ToString(mode)},
      {"generic_key", unsupported.generic_key},
      {"requested_value", unsupported.requested_value},
      {"reason", unsupported.reason},
  };
}

std::map<std::string, std::string>
BuildConfigAdjustedPayload(const core::schema::RunInfo& run_info,
                           const backends::real_sdk::AppliedParam& adjusted,
                           const backends::real_sdk::ParamApplyMode mode) {
  return {
      {"run_id", run_info.run_id},
      {"scenario_id", run_info.config.scenario_id},
      {"apply_mode", backends::real_sdk::ToString(mode)},
      {"generic_key", adjusted.generic_key},
      {"node_name", adjusted.node_name},
      {"requested_value", adjusted.requested_value},
      {"applied_value", adjusted.applied_value},
      {"reason", adjusted.adjustment_reason},
  };
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

  if (!backends::real_sdk::ApplyParams(backend, key_map, *adapter, run_plan.real_params,
                                       run_plan.real_apply_mode, apply_result, error)) {
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
      if (!AppendTraceEvent(
              events::EventType::kConfigUnsupported, std::chrono::system_clock::now(),
              BuildConfigUnsupportedPayload(run_info, unsupported, run_plan.real_apply_mode),
              bundle_dir, events_path, event_error)) {
        logger.Warn("failed to append CONFIG_UNSUPPORTED event on strict apply failure",
                    {{"error", event_error}});
      }
    }
    return false;
  }

  for (const auto& unsupported : apply_result.unsupported) {
    if (!AppendTraceEvent(
            events::EventType::kConfigUnsupported, std::chrono::system_clock::now(),
            BuildConfigUnsupportedPayload(run_info, unsupported, run_plan.real_apply_mode),
            bundle_dir, events_path, error)) {
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
    if (!AppendTraceEvent(events::EventType::kConfigAdjusted, std::chrono::system_clock::now(),
                          BuildConfigAdjustedPayload(run_info, applied, run_plan.real_apply_mode),
                          bundle_dir, events_path, error)) {
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

void AppendArtifactIfPresent(std::vector<fs::path>& artifact_paths, const fs::path& path) {
  if (!path.empty() && fs::exists(path)) {
    artifact_paths.push_back(path);
  }
}

// Both pause and completion paths publish the same manifest contract; keep this
// assembly centralized so optional artifact behavior stays aligned.
std::vector<fs::path>
BuildBundleManifestArtifactPaths(std::vector<fs::path> required_artifacts,
                                 const std::vector<fs::path>& optional_artifacts,
                                 const std::vector<fs::path>& hostprobe_raw_artifact_paths) {
  for (const fs::path& path : optional_artifacts) {
    AppendArtifactIfPresent(required_artifacts, path);
  }
  required_artifacts.insert(required_artifacts.end(), hostprobe_raw_artifact_paths.begin(),
                            hostprobe_raw_artifact_paths.end());
  return required_artifacts;
}

// Centralized run execution keeps `run` and `baseline capture` behavior aligned
// so artifact contracts and metrics math never diverge between modes.
int ExecuteScenarioRunInternal(const RunOptions& options, bool use_per_run_bundle_dir,
                               bool allow_zip_bundle, std::string_view success_prefix,
                               ScenarioRunResult* run_result) {
  core::logging::Logger logger(options.log_level);

  if (run_result != nullptr) {
    *run_result = ScenarioRunResult{};
  }

  logger.Info(
      "run execution requested",
      {{"scenario_path", options.scenario_path},
       {"output_root", options.output_dir.string()},
       {"zip_bundle", options.zip_bundle ? "true" : "false"},
       {"redact", options.redact_identifiers ? "true" : "false"},
       {"soak_mode", options.soak_mode ? "true" : "false"},
       {"netem_apply", options.apply_netem ? "true" : "false"},
       {"device_selector", options.device_selector.empty() ? "-" : options.device_selector}});

  std::string error;
  if (options.soak_mode && !use_per_run_bundle_dir) {
    logger.Error("soak mode is only supported for per-run bundle execution");
    std::cerr << "error: soak mode is only supported by labops run\n";
    return kExitUsage;
  }
  if (options.soak_mode && options.checkpoint_interval <= std::chrono::milliseconds::zero()) {
    logger.Error("invalid soak checkpoint interval");
    std::cerr << "error: checkpoint interval must be greater than 0 milliseconds\n";
    return kExitUsage;
  }
  if (options.zip_bundle && !allow_zip_bundle) {
    logger.Error("zip output is not supported for this command");
    std::cerr << "error: zip output is not supported for this command\n";
    return kExitUsage;
  }

  if (!ValidateScenarioPath(options.scenario_path, error)) {
    logger.Error("scenario path validation failed",
                 {{"scenario_path", options.scenario_path}, {"error", error}});
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  RunPlan run_plan;
  if (!LoadRunPlanFromScenario(options.scenario_path, run_plan, error)) {
    logger.Error("failed to load run plan from scenario",
                 {{"scenario_path", options.scenario_path}, {"error", error}});
    std::cerr << "error: " << error << '\n';
    return kExitSchemaInvalid;
  }

  std::optional<ResolvedDeviceSelection> resolved_device_selection;
  if (!ResolveDeviceSelectionForRun(run_plan, options, resolved_device_selection, error)) {
    logger.Error("device selector resolution failed", {{"error", error}});
    std::cerr << "error: device selector resolution failed: " << error << '\n';
    return kExitFailure;
  }
  if (resolved_device_selection.has_value()) {
    const ResolvedDeviceSelection& selected = resolved_device_selection.value();
    logger.Info(
        "device selector resolved",
        {{"selector", selected.selector_text},
         {"selected_index", std::to_string(selected.discovered_index)},
         {"selected_model", selected.device.model},
         {"selected_serial", selected.device.serial},
         {"selected_user_id", selected.device.user_id.empty() ? "(none)" : selected.device.user_id},
         {"selected_transport", selected.device.transport}});
    if (selected.device.firmware_version.has_value()) {
      logger.Info("device selector firmware detected",
                  {{"selected_firmware_version", selected.device.firmware_version.value()}});
    }
    if (selected.device.sdk_version.has_value()) {
      logger.Info("device selector sdk version detected",
                  {{"selected_sdk_version", selected.device.sdk_version.value()}});
    }
  }

  std::optional<artifacts::NetemCommandSuggestions> netem_suggestions;
  std::string netem_warning;
  if (!BuildNetemCommandSuggestions(options.scenario_path, run_plan, netem_suggestions,
                                    netem_warning)) {
    logger.Error("failed to build netem command suggestions");
    std::cerr << "error: failed to build netem command suggestions\n";
    return kExitFailure;
  }
  if (!netem_warning.empty()) {
    logger.Warn("netem suggestion warning", {{"warning", netem_warning}});
    std::cerr << "warning: " << netem_warning << '\n';
  }

  const bool is_resume = options.soak_mode && !options.resume_checkpoint_path.empty();
  soak::CheckpointState resume_checkpoint;
  std::chrono::milliseconds completed_duration{0};
  std::vector<backends::FrameSample> frames;

  const auto created_at = std::chrono::system_clock::now();
  core::schema::RunInfo run_info = BuildRunInfo(options, run_plan, created_at);
  AttachResolvedDeviceMetadataToRunInfo(resolved_device_selection, run_info);
  fs::path bundle_dir = ResolveExecutionOutputDir(options, run_info, use_per_run_bundle_dir);
  fs::path soak_frame_cache_path;
  fs::path soak_checkpoint_latest_path;
  fs::path soak_checkpoint_history_path;

  if (is_resume) {
    if (!soak::LoadCheckpoint(options.resume_checkpoint_path, resume_checkpoint, error)) {
      logger.Error("failed to load soak checkpoint",
                   {{"checkpoint", options.resume_checkpoint_path.string()}, {"error", error}});
      std::cerr << "error: failed to load soak checkpoint: " << error << '\n';
      return kExitFailure;
    }

    if (fs::path(options.scenario_path).lexically_normal() !=
        resume_checkpoint.scenario_path.lexically_normal()) {
      logger.Error("resume scenario mismatch",
                   {{"scenario_path", options.scenario_path},
                    {"checkpoint_scenario", resume_checkpoint.scenario_path.string()}});
      std::cerr << "error: resume scenario mismatch: expected "
                << resume_checkpoint.scenario_path.string() << '\n';
      return kExitFailure;
    }
    if (resume_checkpoint.status == soak::CheckpointStatus::kCompleted) {
      logger.Error("resume requested for already completed checkpoint");
      std::cerr << "error: checkpoint is already completed\n";
      return kExitFailure;
    }
    if (resume_checkpoint.completed_duration >= resume_checkpoint.total_duration) {
      logger.Error("resume requested but checkpoint has no remaining duration");
      std::cerr << "error: checkpoint has no remaining soak duration\n";
      return kExitFailure;
    }
    if (run_plan.duration.count() != resume_checkpoint.total_duration.count()) {
      logger.Error(
          "resume duration mismatch",
          {{"scenario_duration_ms", std::to_string(run_plan.duration.count())},
           {"checkpoint_duration_ms", std::to_string(resume_checkpoint.total_duration.count())}});
      std::cerr << "error: scenario duration does not match checkpoint duration\n";
      return kExitFailure;
    }

    run_info.run_id = resume_checkpoint.run_id;
    run_info.timestamps.created_at = resume_checkpoint.timestamps.created_at;
    run_info.timestamps.started_at = resume_checkpoint.timestamps.started_at;
    run_info.timestamps.finished_at = resume_checkpoint.timestamps.finished_at;
    completed_duration = resume_checkpoint.completed_duration;
    bundle_dir = resume_checkpoint.bundle_dir;
    soak_frame_cache_path = resume_checkpoint.frame_cache_path.empty()
                                ? (bundle_dir / "soak_frames.jsonl")
                                : resume_checkpoint.frame_cache_path;

    if (!soak::LoadFrameCache(soak_frame_cache_path, frames, error)) {
      logger.Error("failed to load soak frame cache",
                   {{"path", soak_frame_cache_path.string()}, {"error", error}});
      std::cerr << "error: failed to load soak frame cache: " << error << '\n';
      return kExitFailure;
    }
  } else if (options.soak_mode) {
    soak_frame_cache_path = bundle_dir / "soak_frames.jsonl";
  }

  logger.SetRunId(run_info.run_id);
  logger.Info("run initialized", {{"scenario_id", run_info.config.scenario_id},
                                  {"backend", run_info.config.backend},
                                  {"bundle_dir", bundle_dir.string()},
                                  {"duration_ms", std::to_string(run_plan.duration.count())}});
  if (run_result != nullptr) {
    run_result->run_id = run_info.run_id;
    run_result->bundle_dir = bundle_dir;
  }

  fs::path scenario_artifact_path = bundle_dir / "scenario.json";
  if (!is_resume || !fs::exists(scenario_artifact_path)) {
    if (!artifacts::WriteScenarioJson(options.scenario_path, bundle_dir, scenario_artifact_path,
                                      error)) {
      logger.Error("failed to write scenario snapshot",
                   {{"bundle_dir", bundle_dir.string()}, {"error", error}});
      std::cerr << "error: failed to write scenario snapshot: " << error << '\n';
      return kExitFailure;
    }
    logger.Debug("scenario snapshot written", {{"path", scenario_artifact_path.string()}});
  } else {
    logger.Info("resume mode reusing existing scenario snapshot",
                {{"path", scenario_artifact_path.string()}});
  }

  fs::path hostprobe_artifact_path = bundle_dir / "hostprobe.json";
  std::vector<fs::path> hostprobe_raw_artifact_paths;
  if (is_resume && fs::exists(hostprobe_artifact_path)) {
    hostprobe_raw_artifact_paths = CollectNicRawArtifactPaths(bundle_dir);
    logger.Info("resume mode reusing existing host probe artifacts",
                {{"hostprobe", hostprobe_artifact_path.string()},
                 {"hostprobe_raw_count", std::to_string(hostprobe_raw_artifact_paths.size())}});
  } else {
    hostprobe::HostProbeSnapshot host_snapshot;
    if (!hostprobe::CollectHostProbeSnapshot(host_snapshot, error)) {
      logger.Error("failed to collect host probe data", {{"error", error}});
      std::cerr << "error: failed to collect host probe data: " << error << '\n';
      return kExitFailure;
    }

    hostprobe::NicProbeSnapshot nic_probe;
    if (!hostprobe::CollectNicProbeSnapshot(nic_probe, error)) {
      logger.Warn("NIC probe collection issue", {{"warning", error}});
      std::cerr << "warning: NIC probe collection issue: " << error << '\n';
    }
    host_snapshot.nic_highlights = nic_probe.highlights;

    if (options.redact_identifiers) {
      hostprobe::IdentifierRedactionContext redaction_context;
      hostprobe::BuildIdentifierRedactionContext(redaction_context);
      hostprobe::RedactHostProbeSnapshot(host_snapshot, redaction_context);
      hostprobe::RedactNicProbeSnapshot(nic_probe, redaction_context);
      host_snapshot.nic_highlights = nic_probe.highlights;
    }

    if (!artifacts::WriteHostProbeJson(host_snapshot, bundle_dir, hostprobe_artifact_path, error)) {
      logger.Error("failed to write host probe artifact", {{"error", error}});
      std::cerr << "error: failed to write hostprobe.json: " << error << '\n';
      return kExitFailure;
    }

    if (!artifacts::WriteHostProbeRawCommandOutputs(nic_probe.raw_captures, bundle_dir,
                                                    hostprobe_raw_artifact_paths, error)) {
      logger.Error("failed to write NIC raw command artifacts", {{"error", error}});
      std::cerr << "error: failed to write NIC raw command artifacts: " << error << '\n';
      return kExitFailure;
    }
  }

  std::unique_ptr<backends::ICameraBackend> backend;
  if (!BuildBackendFromRunPlan(run_plan, backend, error)) {
    logger.Error("backend selection failed", {{"error", error}});
    std::cerr << "error: backend selection failed: " << error << '\n';
    return kExitFailure;
  }

  backends::BackendConfig selected_device_params;
  if (resolved_device_selection.has_value() &&
      !ApplyDeviceSelectionToBackend(*backend, resolved_device_selection.value(),
                                     selected_device_params, error)) {
    logger.Error("failed to apply resolved device selector", {{"error", error}});
    std::cerr << "error: failed to apply resolved device selector: " << error << '\n';
    return kExitFailure;
  }

  fs::path events_path;
  fs::path config_verify_artifact_path;
  fs::path camera_config_artifact_path;
  fs::path config_report_artifact_path;
  backends::BackendConfig applied_params;
  for (const auto& [key, value] : selected_device_params) {
    applied_params[key] = value;
  }

  bool config_applied_event_emitted = false;
  if (run_plan.backend == kBackendRealStub) {
    if (!ApplyRealParamsWithEvents(*backend, run_plan, run_info, bundle_dir, applied_params,
                                   events_path, config_verify_artifact_path,
                                   camera_config_artifact_path, config_report_artifact_path, logger,
                                   error)) {
      logger.Error("backend config apply failed", {{"error", error}});
      std::cerr << "error: backend config failed: " << error << '\n';
      return kExitFailure;
    }

    if (!AppendTraceEvent(events::EventType::kConfigApplied, std::chrono::system_clock::now(),
                          BuildConfigAppliedPayload(run_info, applied_params), bundle_dir,
                          events_path, error)) {
      logger.Error("failed to append CONFIG_APPLIED event", {{"error", error}});
      std::cerr << "error: failed to append CONFIG_APPLIED event: " << error << '\n';
      return kExitFailure;
    }
    config_applied_event_emitted = true;
  }

  if (!backend->Connect(error)) {
    logger.Error("backend connect failed",
                 {{"backend", run_info.config.backend}, {"error", error}});
    run_info.timestamps.finished_at = std::chrono::system_clock::now();
    fs::path run_artifact_path;
    std::string run_write_error;
    if (!artifacts::WriteRunJson(run_info, bundle_dir, run_artifact_path, run_write_error)) {
      logger.Error("failed to write run.json after backend connect failure",
                   {{"error", run_write_error}});
      std::cerr << "warning: failed to write run.json after backend connect failure: "
                << run_write_error << '\n';
    } else if (run_result != nullptr) {
      run_result->run_json_path = run_artifact_path;
    }
    if (!config_verify_artifact_path.empty()) {
      std::cerr << "info: config verify artifact: " << config_verify_artifact_path.string() << '\n';
    }
    if (!camera_config_artifact_path.empty()) {
      std::cerr << "info: camera config artifact: " << camera_config_artifact_path.string() << '\n';
    }
    if (!config_report_artifact_path.empty()) {
      std::cerr << "info: config report artifact: " << config_report_artifact_path.string() << '\n';
    }
    std::cerr << "error: backend connect failed: " << error << '\n';
    return kExitBackendConnectFailed;
  }
  logger.Info("backend connected", {{"backend", run_info.config.backend}});

  if (run_plan.backend == kBackendSim) {
    if (!backends::sim::ApplyScenarioConfig(*backend, run_plan.sim_config, error,
                                            &applied_params)) {
      logger.Error("backend config apply failed", {{"error", error}});
      std::cerr << "error: backend config failed: " << error << '\n';
      return kExitFailure;
    }
  }
  logger.Debug("backend config applied", {{"param_count", std::to_string(applied_params.size())}});

  if (!config_applied_event_emitted) {
    const auto config_applied_at = std::chrono::system_clock::now();
    if (!AppendTraceEvent(events::EventType::kConfigApplied, config_applied_at,
                          BuildConfigAppliedPayload(run_info, applied_params), bundle_dir,
                          events_path, error)) {
      logger.Error("failed to append CONFIG_APPLIED event", {{"error", error}});
      std::cerr << "error: failed to append CONFIG_APPLIED event: " << error << '\n';
      return kExitFailure;
    }
  }

  ScopedNetemTeardown netem_teardown_guard(&logger);
  if (!ApplyNetemIfRequested(options, netem_suggestions, netem_teardown_guard, error)) {
    logger.Error("netem apply failed", {{"error", error}});
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }

  if (!backend->Start(error)) {
    logger.Error("backend start failed", {{"error", error}});
    std::cerr << "error: backend start failed: " << error << '\n';
    return kExitFailure;
  }
  logger.Info("stream started", {{"fps", std::to_string(run_plan.sim_config.fps)},
                                 {"duration_ms", std::to_string(run_plan.duration.count())}});

  bool stream_started = true;
  const auto started_at = std::chrono::system_clock::now();
  if (!is_resume) {
    run_info.timestamps.started_at = started_at;
  }

  auto stop_if_started = [&]() {
    if (!stream_started) {
      return;
    }
    std::string stop_error;
    (void)backend->Stop(stop_error);
    stream_started = false;
  };

  if (!AppendTraceEvent(events::EventType::kStreamStarted, started_at,
                        {
                            {"run_id", run_info.run_id},
                            {"scenario_id", run_info.config.scenario_id},
                            {"backend", run_info.config.backend},
                            {"duration_ms", std::to_string(run_plan.duration.count())},
                            {"fps", std::to_string(run_plan.sim_config.fps)},
                            {"seed", std::to_string(run_plan.sim_config.seed)},
                            {"soak_mode", options.soak_mode ? "true" : "false"},
                            {"resume", is_resume ? "true" : "false"},
                        },
                        bundle_dir, events_path, error)) {
    logger.Error("failed to append STREAM_STARTED event", {{"error", error}});
    stop_if_started();
    std::cerr << "error: failed to append STREAM_STARTED event: " << error << '\n';
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
    if (dropped) {
      ++dropped_count;
    } else {
      ++received_count;
    }
  }

  auto append_frame_event = [&](const backends::FrameSample& frame) {
    const bool dropped = frame.dropped.has_value() && frame.dropped.value();
    events::EventType event_type = events::EventType::kFrameReceived;
    std::string drop_reason;
    switch (frame.outcome) {
    case backends::FrameOutcome::kDropped:
      event_type = events::EventType::kFrameDropped;
      drop_reason = "sim_fault_injection";
      break;
    case backends::FrameOutcome::kTimeout:
      event_type = events::EventType::kFrameTimeout;
      drop_reason = "acquisition_timeout";
      break;
    case backends::FrameOutcome::kIncomplete:
      event_type = events::EventType::kFrameIncomplete;
      drop_reason = "incomplete_frame";
      break;
    case backends::FrameOutcome::kReceived:
    default:
      event_type = events::EventType::kFrameReceived;
      break;
    }

    if (!latest_frame_ts.has_value() || frame.timestamp > latest_frame_ts.value()) {
      latest_frame_ts = frame.timestamp;
    }
    if (dropped) {
      ++dropped_count;
    } else {
      ++received_count;
    }

    std::map<std::string, std::string> payload = {
        {"run_id", run_info.run_id},
        {"frame_id", std::to_string(frame.frame_id)},
        {"size_bytes", std::to_string(frame.size_bytes)},
        {"dropped", dropped ? "true" : "false"},
    };
    if (dropped) {
      payload["reason"] = drop_reason.empty() ? "backend_marked_dropped" : drop_reason;
    }
    return AppendTraceEvent(event_type, frame.timestamp, std::move(payload), bundle_dir,
                            events_path, error);
  };

  ScopedInterruptSignalHandler scoped_signal_handler;
  bool interrupted_by_signal = false;
  std::chrono::milliseconds non_soak_completed_duration = run_plan.duration;
  if (!options.soak_mode) {
    if (run_plan.backend != kBackendRealStub) {
      const std::vector<backends::FrameSample> pulled_frames =
          backend->PullFrames(run_plan.duration, error);
      if (!error.empty()) {
        logger.Error("backend pull_frames failed", {{"error", error}});
        stop_if_started();
        std::cerr << "error: backend pull_frames failed: " << error << '\n';
        return kExitFailure;
      }

      for (const auto& frame : pulled_frames) {
        if (!append_frame_event(frame)) {
          logger.Error("failed to append frame event", {{"error", error}});
          stop_if_started();
          std::cerr << "error: failed to append frame event: " << error << '\n';
          return kExitFailure;
        }
        frames.push_back(frame);
      }
    } else {
      // Real-backend runs are chunked so Ctrl+C can stop at safe boundaries
      // without dropping partially written artifacts.
      constexpr std::chrono::milliseconds kInterruptPollInterval(250);
      non_soak_completed_duration = std::chrono::milliseconds::zero();
      std::chrono::milliseconds remaining_duration = run_plan.duration;
      while (remaining_duration > std::chrono::milliseconds::zero()) {
        if (g_run_interrupt_requested.load()) {
          interrupted_by_signal = true;
          break;
        }

        const std::chrono::milliseconds chunk_duration =
            std::min(kInterruptPollInterval, remaining_duration);
        const std::vector<backends::FrameSample> pulled_frames =
            backend->PullFrames(chunk_duration, error);
        if (!error.empty()) {
          logger.Error("backend pull_frames failed", {{"error", error}});
          stop_if_started();
          std::cerr << "error: backend pull_frames failed: " << error << '\n';
          return kExitFailure;
        }

        for (const auto& frame : pulled_frames) {
          if (!append_frame_event(frame)) {
            logger.Error("failed to append frame event", {{"error", error}});
            stop_if_started();
            std::cerr << "error: failed to append frame event: " << error << '\n';
            return kExitFailure;
          }
          frames.push_back(frame);
        }

        non_soak_completed_duration += chunk_duration;
        if (non_soak_completed_duration > run_plan.duration) {
          non_soak_completed_duration = run_plan.duration;
        }
        remaining_duration = run_plan.duration - non_soak_completed_duration;
      }

      if (interrupted_by_signal) {
        logger.Warn("interrupt received; finalizing run with partial duration",
                    {{"completed_duration_ms", std::to_string(non_soak_completed_duration.count())},
                     {"requested_duration_ms", std::to_string(run_plan.duration.count())}});
      }
    }
  } else {
    if (soak_frame_cache_path.empty()) {
      soak_frame_cache_path = bundle_dir / "soak_frames.jsonl";
    }
    if (completed_duration > run_plan.duration) {
      logger.Error("resume checkpoint has invalid completed duration");
      stop_if_started();
      std::cerr << "error: checkpoint completed duration exceeds total run duration\n";
      return kExitFailure;
    }

    std::chrono::milliseconds remaining_duration = run_plan.duration - completed_duration;
    soak::CheckpointState checkpoint_state;
    checkpoint_state.run_id = run_info.run_id;
    checkpoint_state.scenario_path = fs::path(options.scenario_path);
    checkpoint_state.bundle_dir = bundle_dir;
    checkpoint_state.frame_cache_path = soak_frame_cache_path;
    checkpoint_state.total_duration = run_plan.duration;
    checkpoint_state.completed_duration = completed_duration;
    checkpoint_state.checkpoints_written = is_resume ? resume_checkpoint.checkpoints_written : 0U;
    checkpoint_state.frames_total = static_cast<std::uint64_t>(frames.size());
    checkpoint_state.frames_received = received_count;
    checkpoint_state.frames_dropped = dropped_count;
    checkpoint_state.timestamps = run_info.timestamps;
    checkpoint_state.updated_at = std::chrono::system_clock::now();
    checkpoint_state.status = soak::CheckpointStatus::kRunning;
    checkpoint_state.stop_reason.clear();

    while (remaining_duration > std::chrono::milliseconds::zero()) {
      const std::chrono::milliseconds chunk_duration =
          std::min(options.checkpoint_interval, remaining_duration);
      std::vector<backends::FrameSample> chunk_frames = backend->PullFrames(chunk_duration, error);
      if (!error.empty()) {
        logger.Error("backend pull_frames failed", {{"error", error}});
        stop_if_started();
        std::cerr << "error: backend pull_frames failed: " << error << '\n';
        return kExitFailure;
      }

      const std::uint64_t frame_id_offset = frames.empty() ? 0U : (frames.back().frame_id + 1U);
      std::vector<backends::FrameSample> normalized_chunk;
      normalized_chunk.reserve(chunk_frames.size());
      for (auto frame : chunk_frames) {
        frame.frame_id += frame_id_offset;
        if (latest_frame_ts.has_value() && frame.timestamp <= latest_frame_ts.value()) {
          frame.timestamp = latest_frame_ts.value() + std::chrono::microseconds(1);
        }

        if (!append_frame_event(frame)) {
          logger.Error("failed to append frame event", {{"error", error}});
          stop_if_started();
          std::cerr << "error: failed to append frame event: " << error << '\n';
          return kExitFailure;
        }

        frames.push_back(frame);
        normalized_chunk.push_back(frame);
      }

      if (!normalized_chunk.empty() &&
          !soak::AppendFrameCache(normalized_chunk, soak_frame_cache_path, error)) {
        logger.Error("failed to append soak frame cache", {{"error", error}});
        stop_if_started();
        std::cerr << "error: failed to append soak frame cache: " << error << '\n';
        return kExitFailure;
      }

      completed_duration += chunk_duration;
      if (completed_duration > run_plan.duration) {
        completed_duration = run_plan.duration;
      }
      remaining_duration = run_plan.duration - completed_duration;

      checkpoint_state.completed_duration = completed_duration;
      checkpoint_state.frames_total = static_cast<std::uint64_t>(frames.size());
      checkpoint_state.frames_received = received_count;
      checkpoint_state.frames_dropped = dropped_count;
      checkpoint_state.updated_at = std::chrono::system_clock::now();
      checkpoint_state.status = soak::CheckpointStatus::kRunning;
      checkpoint_state.stop_reason.clear();
      ++checkpoint_state.checkpoints_written;
      if (!soak::WriteCheckpointArtifacts(checkpoint_state, soak_checkpoint_latest_path,
                                          soak_checkpoint_history_path, error)) {
        logger.Error("failed to write soak checkpoint", {{"error", error}});
        stop_if_started();
        std::cerr << "error: failed to write soak checkpoint: " << error << '\n';
        return kExitFailure;
      }

      if (!AppendTraceEvent(
              events::EventType::kInfo, checkpoint_state.updated_at,
              {
                  {"run_id", run_info.run_id},
                  {"kind", "SOAK_CHECKPOINT"},
                  {"checkpoint_index", std::to_string(checkpoint_state.checkpoints_written)},
                  {"completed_duration_ms", std::to_string(completed_duration.count())},
                  {"remaining_duration_ms", std::to_string(remaining_duration.count())},
              },
              bundle_dir, events_path, error)) {
        logger.Error("failed to append SOAK_CHECKPOINT event", {{"error", error}});
        stop_if_started();
        std::cerr << "error: failed to append SOAK_CHECKPOINT event: " << error << '\n';
        return kExitFailure;
      }

      const std::string stop_reason = ResolveSoakStopReason(options);
      if (!stop_reason.empty() && remaining_duration > std::chrono::milliseconds::zero()) {
        checkpoint_state.status = soak::CheckpointStatus::kPaused;
        checkpoint_state.stop_reason = stop_reason;
        checkpoint_state.timestamps.finished_at = std::chrono::system_clock::now();
        checkpoint_state.updated_at = checkpoint_state.timestamps.finished_at;
        run_info.timestamps.finished_at = checkpoint_state.timestamps.finished_at;
        if (latest_frame_ts.has_value() &&
            run_info.timestamps.finished_at < latest_frame_ts.value()) {
          run_info.timestamps.finished_at = latest_frame_ts.value();
          checkpoint_state.timestamps.finished_at = latest_frame_ts.value();
          checkpoint_state.updated_at = latest_frame_ts.value();
        }
        if (!soak::WriteCheckpointArtifacts(checkpoint_state, soak_checkpoint_latest_path,
                                            soak_checkpoint_history_path, error)) {
          logger.Error("failed to persist paused soak checkpoint", {{"error", error}});
          stop_if_started();
          std::cerr << "error: failed to persist paused soak checkpoint: " << error << '\n';
          return kExitFailure;
        }

        stop_if_started();
        if (!AppendTraceEvent(
                events::EventType::kStreamStopped, run_info.timestamps.finished_at,
                {
                    {"run_id", run_info.run_id},
                    {"frames_total", std::to_string(frames.size())},
                    {"frames_received", std::to_string(received_count)},
                    {"frames_dropped", std::to_string(dropped_count)},
                    {"reason", "soak_paused"},
                    {"completed_duration_ms", std::to_string(completed_duration.count())},
                    {"remaining_duration_ms", std::to_string(remaining_duration.count())},
                },
                bundle_dir, events_path, error)) {
          logger.Error("failed to append STREAM_STOPPED pause event", {{"error", error}});
          std::cerr << "error: failed to append STREAM_STOPPED pause event: " << error << '\n';
          return kExitFailure;
        }

        fs::path run_artifact_path;
        if (!artifacts::WriteRunJson(run_info, bundle_dir, run_artifact_path, error)) {
          logger.Error("failed to write run.json during soak pause", {{"error", error}});
          std::cerr << "error: failed to write run.json during soak pause: " << error << '\n';
          return kExitFailure;
        }
        if (run_result != nullptr) {
          run_result->run_json_path = run_artifact_path;
          run_result->events_jsonl_path = events_path;
        }

        fs::path bundle_manifest_path;
        const std::vector<fs::path> pause_optional_artifacts = {
            config_verify_artifact_path,
            camera_config_artifact_path,
            config_report_artifact_path,
        };
        std::vector<fs::path> bundle_artifact_paths = BuildBundleManifestArtifactPaths(
            {
                scenario_artifact_path,
                hostprobe_artifact_path,
                run_artifact_path,
                events_path,
                soak_checkpoint_latest_path,
                soak_checkpoint_history_path,
                soak_frame_cache_path,
            },
            pause_optional_artifacts, hostprobe_raw_artifact_paths);
        if (!artifacts::WriteBundleManifestJson(bundle_dir, bundle_artifact_paths,
                                                bundle_manifest_path, error)) {
          logger.Error("failed to write bundle manifest during soak pause", {{"error", error}});
          std::cerr << "error: failed to write bundle manifest during soak pause: " << error
                    << '\n';
          return kExitFailure;
        }

        logger.Info("soak run paused safely", {{"run_id", run_info.run_id},
                                               {"bundle_dir", bundle_dir.string()},
                                               {"checkpoint", soak_checkpoint_latest_path.string()},
                                               {"reason", stop_reason}});

        std::cout << success_prefix << options.scenario_path << '\n';
        std::cout << "run_id: " << run_info.run_id << '\n';
        std::cout << "bundle: " << bundle_dir.string() << '\n';
        std::cout << "events: " << events_path.string() << '\n';
        if (!config_verify_artifact_path.empty()) {
          std::cout << "config_verify: " << config_verify_artifact_path.string() << '\n';
        }
        if (!camera_config_artifact_path.empty()) {
          std::cout << "camera_config: " << camera_config_artifact_path.string() << '\n';
        }
        if (!config_report_artifact_path.empty()) {
          std::cout << "config_report: " << config_report_artifact_path.string() << '\n';
        }
        std::cout << "artifact: " << run_artifact_path.string() << '\n';
        std::cout << "manifest: " << bundle_manifest_path.string() << '\n';
        std::cout << "soak_mode: enabled\n";
        std::cout << "soak_status: paused\n";
        std::cout << "soak_checkpoint: " << soak_checkpoint_latest_path.string() << '\n';
        std::cout << "soak_frame_cache: " << soak_frame_cache_path.string() << '\n';
        std::cout << "soak_completed_duration_ms: " << completed_duration.count() << '\n';
        std::cout << "soak_remaining_duration_ms: " << remaining_duration.count() << '\n';
        return kExitSuccess;
      }
    }

    checkpoint_state.status = soak::CheckpointStatus::kCompleted;
    checkpoint_state.stop_reason = "completed";
    checkpoint_state.completed_duration = run_plan.duration;
    checkpoint_state.frames_total = static_cast<std::uint64_t>(frames.size());
    checkpoint_state.frames_received = received_count;
    checkpoint_state.frames_dropped = dropped_count;
    checkpoint_state.timestamps.finished_at = std::chrono::system_clock::now();
    checkpoint_state.updated_at = checkpoint_state.timestamps.finished_at;
    run_info.timestamps.finished_at = checkpoint_state.timestamps.finished_at;
    if (latest_frame_ts.has_value() && run_info.timestamps.finished_at < latest_frame_ts.value()) {
      run_info.timestamps.finished_at = latest_frame_ts.value();
      checkpoint_state.timestamps.finished_at = latest_frame_ts.value();
      checkpoint_state.updated_at = latest_frame_ts.value();
    }
    ++checkpoint_state.checkpoints_written;
    if (!soak::WriteCheckpointArtifacts(checkpoint_state, soak_checkpoint_latest_path,
                                        soak_checkpoint_history_path, error)) {
      logger.Error("failed to write final soak checkpoint", {{"error", error}});
      stop_if_started();
      std::cerr << "error: failed to write final soak checkpoint: " << error << '\n';
      return kExitFailure;
    }
  }

  if (!backend->Stop(error)) {
    logger.Error("backend stop failed", {{"error", error}});
    std::cerr << "error: backend stop failed: " << error << '\n';
    return kExitFailure;
  }
  stream_started = false;

  if (!options.soak_mode) {
    auto finished_at = std::chrono::system_clock::now();
    if (latest_frame_ts.has_value() && finished_at < latest_frame_ts.value()) {
      finished_at = latest_frame_ts.value();
    }
    run_info.timestamps.finished_at = finished_at;
  }

  std::string stream_stop_reason = options.soak_mode ? "soak_completed" : "completed";
  if (!options.soak_mode && interrupted_by_signal) {
    stream_stop_reason = "signal_interrupt";
  }
  std::map<std::string, std::string> stream_stopped_payload = {
      {"run_id", run_info.run_id},
      {"frames_total", std::to_string(frames.size())},
      {"frames_received", std::to_string(received_count)},
      {"frames_dropped", std::to_string(dropped_count)},
      {"reason", stream_stop_reason},
  };
  if (!options.soak_mode && interrupted_by_signal) {
    stream_stopped_payload["requested_duration_ms"] = std::to_string(run_plan.duration.count());
    stream_stopped_payload["completed_duration_ms"] =
        std::to_string(non_soak_completed_duration.count());
  }

  if (!AppendTraceEvent(events::EventType::kStreamStopped, run_info.timestamps.finished_at,
                        std::move(stream_stopped_payload), bundle_dir, events_path, error)) {
    logger.Error("failed to append STREAM_STOPPED event", {{"error", error}});
    std::cerr << "error: failed to append STREAM_STOPPED event: " << error << '\n';
    return kExitFailure;
  }

  fs::path run_artifact_path;
  if (!artifacts::WriteRunJson(run_info, bundle_dir, run_artifact_path, error)) {
    logger.Error("failed to write run.json", {{"error", error}});
    std::cerr << "error: " << error << '\n';
    return kExitFailure;
  }
  if (run_result != nullptr) {
    run_result->run_json_path = run_artifact_path;
    run_result->events_jsonl_path = events_path;
  }

  std::chrono::milliseconds metrics_duration = run_plan.duration;
  if (!options.soak_mode && interrupted_by_signal) {
    metrics_duration = non_soak_completed_duration;
    if (metrics_duration <= std::chrono::milliseconds::zero()) {
      metrics_duration = std::chrono::milliseconds(1);
    }
  }

  metrics::FpsReport fps_report;
  if (!metrics::ComputeFpsReport(frames, metrics_duration, std::chrono::milliseconds(1'000),
                                 fps_report, error)) {
    logger.Error("failed to compute metrics", {{"error", error}});
    std::cerr << "error: failed to compute fps metrics: " << error << '\n';
    return kExitFailure;
  }

  fs::path metrics_csv_path;
  if (!artifacts::WriteMetricsCsv(fps_report, bundle_dir, metrics_csv_path, error)) {
    logger.Error("failed to write metrics.csv", {{"error", error}});
    std::cerr << "error: failed to write metrics.csv: " << error << '\n';
    return kExitFailure;
  }

  fs::path metrics_json_path;
  if (!artifacts::WriteMetricsJson(fps_report, bundle_dir, metrics_json_path, error)) {
    logger.Error("failed to write metrics.json", {{"error", error}});
    std::cerr << "error: failed to write metrics.json: " << error << '\n';
    return kExitFailure;
  }
  if (run_result != nullptr) {
    run_result->metrics_json_path = metrics_json_path;
  }

  std::vector<std::string> threshold_failures;
  bool thresholds_passed = true;
  if (interrupted_by_signal) {
    thresholds_passed = false;
    threshold_failures.push_back("run interrupted by signal before requested duration completed");
  } else {
    thresholds_passed = EvaluateRunThresholds(run_plan.thresholds, fps_report, threshold_failures);
  }
  if (run_result != nullptr) {
    run_result->thresholds_passed = thresholds_passed;
  }
  const std::vector<std::string> top_anomalies =
      metrics::BuildAnomalyHighlights(fps_report, run_plan.sim_config.fps, threshold_failures);

  fs::path summary_markdown_path;
  if (!artifacts::WriteRunSummaryMarkdown(
          run_info, fps_report, run_plan.sim_config.fps, thresholds_passed, threshold_failures,
          top_anomalies, netem_suggestions, bundle_dir, summary_markdown_path, error)) {
    logger.Error("failed to write summary.md", {{"error", error}});
    std::cerr << "error: failed to write summary.md: " << error << '\n';
    return kExitFailure;
  }

  fs::path report_html_path;
  if (!artifacts::WriteRunSummaryHtml(run_info, fps_report, run_plan.sim_config.fps,
                                      thresholds_passed, threshold_failures, top_anomalies,
                                      bundle_dir, report_html_path, error)) {
    logger.Error("failed to write report.html", {{"error", error}});
    std::cerr << "error: failed to write report.html: " << error << '\n';
    return kExitFailure;
  }

  fs::path bundle_manifest_path;
  std::vector<fs::path> completion_optional_artifacts = {
      config_verify_artifact_path,
      camera_config_artifact_path,
      config_report_artifact_path,
  };
  if (options.soak_mode) {
    completion_optional_artifacts.push_back(soak_frame_cache_path);
    completion_optional_artifacts.push_back(soak_checkpoint_latest_path);
    completion_optional_artifacts.push_back(soak_checkpoint_history_path);
  }
  std::vector<fs::path> bundle_artifact_paths = BuildBundleManifestArtifactPaths(
      {
          scenario_artifact_path,
          hostprobe_artifact_path,
          run_artifact_path,
          events_path,
          metrics_csv_path,
          metrics_json_path,
          summary_markdown_path,
          report_html_path,
      },
      completion_optional_artifacts, hostprobe_raw_artifact_paths);
  if (!artifacts::WriteBundleManifestJson(bundle_dir, bundle_artifact_paths, bundle_manifest_path,
                                          error)) {
    logger.Error("failed to write bundle manifest", {{"error", error}});
    std::cerr << "error: failed to write bundle manifest: " << error << '\n';
    return kExitFailure;
  }

  fs::path bundle_zip_path;
  if (options.zip_bundle) {
    if (!artifacts::WriteBundleZip(bundle_dir, bundle_zip_path, error)) {
      logger.Error("failed to write support bundle zip", {{"error", error}});
      std::cerr << "error: failed to write support bundle zip: " << error << '\n';
      return kExitFailure;
    }
  }

  logger.Info("run artifacts written",
              {{"bundle_dir", bundle_dir.string()},
               {"events", events_path.string()},
               {"config_verify",
                config_verify_artifact_path.empty() ? "-" : config_verify_artifact_path.string()},
               {"camera_config",
                camera_config_artifact_path.empty() ? "-" : camera_config_artifact_path.string()},
               {"config_report",
                config_report_artifact_path.empty() ? "-" : config_report_artifact_path.string()},
               {"metrics_json", metrics_json_path.string()},
               {"summary", summary_markdown_path.string()},
               {"report_html", report_html_path.string()}});

  std::cout << success_prefix << options.scenario_path << '\n';
  std::cout << "run_id: " << run_info.run_id << '\n';
  std::cout << "bundle: " << bundle_dir.string() << '\n';
  std::cout << "scenario: " << scenario_artifact_path.string() << '\n';
  std::cout << "hostprobe: " << hostprobe_artifact_path.string() << '\n';
  std::cout << "hostprobe_raw_count: " << hostprobe_raw_artifact_paths.size() << '\n';
  std::cout << "redaction: " << (options.redact_identifiers ? "enabled" : "disabled") << '\n';
  std::cout << "soak_mode: " << (options.soak_mode ? "enabled" : "disabled") << '\n';
  if (options.soak_mode) {
    std::cout << "soak_checkpoint_interval_ms: " << options.checkpoint_interval.count() << '\n';
    if (!soak_checkpoint_latest_path.empty()) {
      std::cout << "soak_checkpoint: " << soak_checkpoint_latest_path.string() << '\n';
    }
    if (!soak_frame_cache_path.empty()) {
      std::cout << "soak_frame_cache: " << soak_frame_cache_path.string() << '\n';
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
  std::cout << "artifact: " << run_artifact_path.string() << '\n';
  std::cout << "events: " << events_path.string() << '\n';
  if (!config_verify_artifact_path.empty()) {
    std::cout << "config_verify: " << config_verify_artifact_path.string() << '\n';
  }
  if (!camera_config_artifact_path.empty()) {
    std::cout << "camera_config: " << camera_config_artifact_path.string() << '\n';
  }
  if (!config_report_artifact_path.empty()) {
    std::cout << "config_report: " << config_report_artifact_path.string() << '\n';
  }
  std::cout << "metrics_csv: " << metrics_csv_path.string() << '\n';
  std::cout << "metrics_json: " << metrics_json_path.string() << '\n';
  std::cout << "summary: " << summary_markdown_path.string() << '\n';
  std::cout << "report_html: " << report_html_path.string() << '\n';
  std::cout << "manifest: " << bundle_manifest_path.string() << '\n';
  if (options.zip_bundle) {
    std::cout << "bundle_zip: " << bundle_zip_path.string() << '\n';
  }
  std::cout << "fps: avg=" << fps_report.avg_fps
            << " rolling_samples=" << fps_report.rolling_samples.size() << '\n';
  std::cout << "drops: total=" << fps_report.dropped_frames_total
            << " generic=" << fps_report.dropped_generic_frames_total
            << " timeout=" << fps_report.timeout_frames_total
            << " incomplete=" << fps_report.incomplete_frames_total
            << " rate_percent=" << fps_report.drop_rate_percent << '\n';
  std::cout << "timing_us: interval_avg=" << fps_report.inter_frame_interval_us.avg_us
            << " interval_p95=" << fps_report.inter_frame_interval_us.p95_us
            << " jitter_avg=" << fps_report.inter_frame_jitter_us.avg_us
            << " jitter_p95=" << fps_report.inter_frame_jitter_us.p95_us << '\n';
  std::cout << "frames: total=" << frames.size() << " received=" << received_count
            << " dropped=" << dropped_count << '\n';
  std::cout << "run_status: " << (interrupted_by_signal ? "interrupted" : "completed") << '\n';
  if (!options.soak_mode && interrupted_by_signal) {
    std::cout << "completed_duration_ms: " << non_soak_completed_duration.count() << '\n';
    std::cout << "requested_duration_ms: " << run_plan.duration.count() << '\n';
  }

  if (interrupted_by_signal) {
    logger.Warn("run interrupted by signal",
                {{"frames_total", std::to_string(frames.size())},
                 {"frames_received", std::to_string(received_count)},
                 {"frames_dropped", std::to_string(dropped_count)},
                 {"completed_duration_ms", std::to_string(non_soak_completed_duration.count())},
                 {"requested_duration_ms", std::to_string(run_plan.duration.count())}});
    std::cerr << "warning: run interrupted by Ctrl+C; finalized partial artifact bundle\n";
    return kExitFailure;
  }

  if (thresholds_passed) {
    logger.Info("run completed", {{"thresholds", "pass"},
                                  {"frames_total", std::to_string(frames.size())},
                                  {"frames_received", std::to_string(received_count)},
                                  {"frames_dropped", std::to_string(dropped_count)}});
    std::cout << "thresholds: pass\n";
    return kExitSuccess;
  }

  logger.Warn("run completed with threshold failures",
              {{"thresholds", "fail"},
               {"failure_count", std::to_string(threshold_failures.size())},
               {"frames_total", std::to_string(frames.size())},
               {"frames_received", std::to_string(received_count)},
               {"frames_dropped", std::to_string(dropped_count)}});
  std::cout << "thresholds: fail count=" << threshold_failures.size() << '\n';
  for (const auto& failure : threshold_failures) {
    logger.Warn("threshold failure", {{"detail", failure}});
    std::cerr << "threshold failed: " << failure << '\n';
  }
  return kExitThresholdsFailed;
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
    std::cerr << "error: DEVICE_DISCOVERY_FAILED: " << error << '\n';
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
