#include "labops/cli/router.hpp"

#include "artifacts/bundle_manifest_writer.hpp"
#include "artifacts/bundle_zip_writer.hpp"
#include "artifacts/hostprobe_writer.hpp"
#include "artifacts/html_report_writer.hpp"
#include "artifacts/kb_draft_writer.hpp"
#include "artifacts/metrics_diff_writer.hpp"
#include "artifacts/metrics_writer.hpp"
#include "artifacts/run_summary_writer.hpp"
#include "artifacts/run_writer.hpp"
#include "artifacts/scenario_writer.hpp"
#include "backends/camera_backend.hpp"
#include "backends/real_sdk/real_backend_factory.hpp"
#include "backends/sim/scenario_config.hpp"
#include "backends/sim/sim_camera_backend.hpp"
#include "core/errors/exit_codes.hpp"
#include "core/schema/run_contract.hpp"
#include "events/event_model.hpp"
#include "events/jsonl_writer.hpp"
#include "hostprobe/system_probe.hpp"
#include "metrics/anomalies.hpp"
#include "metrics/fps.hpp"
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

// SIGINT is handled as a best-effort safe pause request for soak runs.
// We only read this flag at checkpoint boundaries, so active chunk processing
// remains deterministic and does not abort mid-write.
std::atomic<bool> g_soak_stop_requested{false};

void HandleInterruptSignal(int /*signal_number*/) {
  g_soak_stop_requested.store(true);
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

// Lightweight string extraction for top-level milestone wiring.
// Supports common JSON string escapes used in scenario/profile metadata.
std::optional<std::string> FindStringJsonField(const std::string& text, std::string_view key) {
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
  if (value_pos >= text.size() || text[value_pos] != '"') {
    return std::nullopt;
  }
  ++value_pos;

  std::string parsed;
  while (value_pos < text.size()) {
    const char c = text[value_pos++];
    if (c == '"') {
      return parsed;
    }
    if (c == '\\') {
      if (value_pos >= text.size()) {
        return std::nullopt;
      }
      const char esc = text[value_pos++];
      switch (esc) {
      case '"':
      case '\\':
      case '/':
        parsed.push_back(esc);
        break;
      case 'b':
        parsed.push_back('\b');
        break;
      case 'f':
        parsed.push_back('\f');
        break;
      case 'n':
        parsed.push_back('\n');
        break;
      case 'r':
        parsed.push_back('\r');
        break;
      case 't':
        parsed.push_back('\t');
        break;
      default:
        return std::nullopt;
      }
      continue;
    }
    parsed.push_back(c);
  }

  return std::nullopt;
}

bool IsValidSlug(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const char c : value) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool ResolveNetemProfilePath(const fs::path& scenario_path, std::string_view profile_id,
                             fs::path& resolved_path) {
  resolved_path.clear();
  if (scenario_path.empty() || profile_id.empty()) {
    return false;
  }

  std::error_code ec;
  const fs::path scenario_absolute = fs::absolute(scenario_path, ec);
  if (ec) {
    return false;
  }

  fs::path cursor = scenario_absolute.parent_path();
  while (!cursor.empty()) {
    const fs::path candidate =
        cursor / "tools" / "netem_profiles" / (std::string(profile_id) + ".json");
    std::error_code exists_ec;
    if (fs::exists(candidate, exists_ec) && !exists_ec &&
        fs::is_regular_file(candidate, exists_ec) && !exists_ec) {
      resolved_path = candidate;
      return true;
    }

    const fs::path parent = cursor.parent_path();
    if (parent.empty() || parent == cursor) {
      break;
    }
    cursor = parent;
  }

  return false;
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

  auto read_non_negative_number = [&](std::string_view key, double& target) {
    const auto value = FindNumberJsonField(profile_text, key);
    if (!value.has_value()) {
      return true;
    }
    if (!std::isfinite(value.value()) || value.value() < 0.0) {
      error = "netem profile field must be a non-negative number for key: " + std::string(key);
      return false;
    }
    target = value.value();
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
  if (!ResolveNetemProfilePath(fs::path(scenario_path), run_plan.netem_profile.value(),
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

  auto assign_non_negative_integer_threshold = [&](std::string_view key,
                                                   std::optional<std::uint64_t>& target) {
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

  if (const auto backend = FindStringJsonField(scenario_text, "backend"); backend.has_value()) {
    if (*backend != kBackendSim && *backend != kBackendRealStub) {
      error = "scenario backend must be one of: sim, real_stub";
      return false;
    }
    plan.backend = *backend;
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
  if (!assign_non_negative_double("max_drop_rate_percent", plan.thresholds.max_drop_rate_percent,
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

  if (const auto netem_profile = FindStringJsonField(scenario_text, "netem_profile");
      netem_profile.has_value()) {
    if (netem_profile->empty()) {
      error = "scenario netem_profile must not be empty";
      return false;
    }
    if (!IsValidSlug(netem_profile.value())) {
      error = "scenario netem_profile must use lowercase slug format [a-z0-9_-]+";
      return false;
    }
    plan.netem_profile = netem_profile.value();
  }

  if (const auto device_selector = FindStringJsonField(scenario_text, "device_selector");
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

enum class SoakCheckpointStatus {
  kRunning,
  kPaused,
  kCompleted,
};

struct SoakCheckpointState {
  std::string run_id;
  fs::path scenario_path;
  fs::path bundle_dir;
  fs::path frame_cache_path;
  std::chrono::milliseconds total_duration{0};
  std::chrono::milliseconds completed_duration{0};
  std::uint64_t checkpoints_written = 0;
  std::uint64_t frames_total = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_dropped = 0;
  core::schema::RunTimestamps timestamps{};
  std::chrono::system_clock::time_point updated_at{};
  SoakCheckpointStatus status = SoakCheckpointStatus::kRunning;
  std::string stop_reason;
};

const char* ToString(SoakCheckpointStatus status) {
  switch (status) {
  case SoakCheckpointStatus::kRunning:
    return "running";
  case SoakCheckpointStatus::kPaused:
    return "paused";
  case SoakCheckpointStatus::kCompleted:
    return "completed";
  }
  return "running";
}

bool ParseSoakCheckpointStatus(std::string_view text, SoakCheckpointStatus& status) {
  if (text == "running") {
    status = SoakCheckpointStatus::kRunning;
    return true;
  }
  if (text == "paused") {
    status = SoakCheckpointStatus::kPaused;
    return true;
  }
  if (text == "completed") {
    status = SoakCheckpointStatus::kCompleted;
    return true;
  }
  return false;
}

std::string EscapeJsonString(std::string_view input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default: {
      const auto as_unsigned = static_cast<unsigned char>(ch);
      if (as_unsigned < 0x20U) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(as_unsigned) << std::dec << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
    }
  }
  return out.str();
}

std::int64_t ToEpochMilliseconds(const std::chrono::system_clock::time_point ts) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
}

std::chrono::system_clock::time_point FromEpochMilliseconds(std::uint64_t epoch_ms) {
  return std::chrono::system_clock::time_point(
      std::chrono::milliseconds(static_cast<std::int64_t>(epoch_ms)));
}

bool WriteSoakCheckpointJson(const SoakCheckpointState& state, const fs::path& output_path,
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

  std::error_code ec;
  fs::create_directories(output_path.parent_path(), ec);
  if (ec) {
    error = "failed to create soak checkpoint directory '" + output_path.parent_path().string() +
            "': " + ec.message();
    return false;
  }

  const std::uint64_t total_ms =
      static_cast<std::uint64_t>(std::max<std::int64_t>(state.total_duration.count(), 0));
  const std::uint64_t completed_ms =
      static_cast<std::uint64_t>(std::max<std::int64_t>(state.completed_duration.count(), 0));
  const std::uint64_t remaining_ms = completed_ms >= total_ms ? 0U : (total_ms - completed_ms);

  std::ofstream out_file(output_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open soak checkpoint output '" + output_path.string() + "'";
    return false;
  }

  out_file << "{\n"
           << "  \"schema_version\": \"1.0\",\n"
           << "  \"mode\": \"soak\",\n"
           << "  \"status\": \"" << ToString(state.status) << "\",\n"
           << "  \"stop_reason\": \"" << EscapeJsonString(state.stop_reason) << "\",\n"
           << "  \"run_id\": \"" << EscapeJsonString(state.run_id) << "\",\n"
           << "  \"scenario_path\": \"" << EscapeJsonString(state.scenario_path.string()) << "\",\n"
           << "  \"bundle_dir\": \"" << EscapeJsonString(state.bundle_dir.string()) << "\",\n"
           << "  \"frame_cache_path\": \"" << EscapeJsonString(state.frame_cache_path.string())
           << "\",\n"
           << "  \"total_duration_ms\": " << total_ms << ",\n"
           << "  \"completed_duration_ms\": " << completed_ms << ",\n"
           << "  \"remaining_duration_ms\": " << remaining_ms << ",\n"
           << "  \"checkpoints_written\": " << state.checkpoints_written << ",\n"
           << "  \"frames_total\": " << state.frames_total << ",\n"
           << "  \"frames_received\": " << state.frames_received << ",\n"
           << "  \"frames_dropped\": " << state.frames_dropped << ",\n"
           << "  \"created_at_epoch_ms\": " << ToEpochMilliseconds(state.timestamps.created_at)
           << ",\n"
           << "  \"started_at_epoch_ms\": " << ToEpochMilliseconds(state.timestamps.started_at)
           << ",\n"
           << "  \"finished_at_epoch_ms\": " << ToEpochMilliseconds(state.timestamps.finished_at)
           << ",\n"
           << "  \"updated_at_epoch_ms\": " << ToEpochMilliseconds(state.updated_at) << ",\n"
           << "  \"resume_hint\": \"labops run " << EscapeJsonString(state.scenario_path.string())
           << " --soak --resume "
           << EscapeJsonString((state.bundle_dir / "soak_checkpoint.json").string()) << "\"\n"
           << "}\n";
  if (!out_file) {
    error = "failed while writing soak checkpoint output '" + output_path.string() + "'";
    return false;
  }

  return true;
}

bool WriteSoakCheckpointArtifacts(const SoakCheckpointState& state,
                                  fs::path& latest_checkpoint_path,
                                  fs::path& history_checkpoint_path, std::string& error) {
  latest_checkpoint_path = state.bundle_dir / "soak_checkpoint.json";
  history_checkpoint_path = state.bundle_dir / "checkpoints" /
                            ("checkpoint_" + std::to_string(state.checkpoints_written) + ".json");

  if (!WriteSoakCheckpointJson(state, latest_checkpoint_path, error)) {
    return false;
  }
  if (!WriteSoakCheckpointJson(state, history_checkpoint_path, error)) {
    return false;
  }
  return true;
}

bool LoadSoakCheckpoint(const fs::path& checkpoint_path, SoakCheckpointState& state,
                        std::string& error) {
  state = SoakCheckpointState{};
  std::string text;
  if (!ReadTextFile(checkpoint_path.string(), text, error)) {
    return false;
  }

  const auto run_id = FindStringJsonField(text, "run_id");
  const auto scenario_path = FindStringJsonField(text, "scenario_path");
  const auto bundle_dir = FindStringJsonField(text, "bundle_dir");
  const auto frame_cache_path = FindStringJsonField(text, "frame_cache_path");
  const auto status_text = FindStringJsonField(text, "status");
  const auto total_duration_ms = FindUnsignedJsonField(text, "total_duration_ms");
  const auto completed_duration_ms = FindUnsignedJsonField(text, "completed_duration_ms");
  const auto checkpoints_written = FindUnsignedJsonField(text, "checkpoints_written");
  const auto frames_total = FindUnsignedJsonField(text, "frames_total");
  const auto frames_received = FindUnsignedJsonField(text, "frames_received");
  const auto frames_dropped = FindUnsignedJsonField(text, "frames_dropped");
  const auto created_at_epoch_ms = FindUnsignedJsonField(text, "created_at_epoch_ms");
  const auto started_at_epoch_ms = FindUnsignedJsonField(text, "started_at_epoch_ms");
  const auto finished_at_epoch_ms = FindUnsignedJsonField(text, "finished_at_epoch_ms");
  const auto updated_at_epoch_ms = FindUnsignedJsonField(text, "updated_at_epoch_ms");

  if (!run_id.has_value() || !scenario_path.has_value() || !bundle_dir.has_value() ||
      !status_text.has_value() || !total_duration_ms.has_value() ||
      !completed_duration_ms.has_value() || !checkpoints_written.has_value() ||
      !frames_total.has_value() || !frames_received.has_value() || !frames_dropped.has_value() ||
      !created_at_epoch_ms.has_value() || !started_at_epoch_ms.has_value() ||
      !finished_at_epoch_ms.has_value() || !updated_at_epoch_ms.has_value()) {
    error = "checkpoint is missing required fields: " + checkpoint_path.string();
    return false;
  }

  SoakCheckpointStatus status = SoakCheckpointStatus::kRunning;
  if (!ParseSoakCheckpointStatus(status_text.value(), status)) {
    error = "checkpoint has unsupported status value: " + status_text.value();
    return false;
  }

  if (completed_duration_ms.value() > total_duration_ms.value()) {
    error = "checkpoint completed_duration_ms exceeds total_duration_ms";
    return false;
  }

  if (run_id->empty() || scenario_path->empty() || bundle_dir->empty()) {
    error = "checkpoint contains empty required identity fields";
    return false;
  }

  state.run_id = *run_id;
  state.scenario_path = fs::path(*scenario_path);
  state.bundle_dir = fs::path(*bundle_dir);
  state.frame_cache_path = frame_cache_path.has_value() && !frame_cache_path->empty()
                               ? fs::path(*frame_cache_path)
                               : state.bundle_dir / "soak_frames.jsonl";
  state.total_duration =
      std::chrono::milliseconds(static_cast<std::int64_t>(total_duration_ms.value()));
  state.completed_duration =
      std::chrono::milliseconds(static_cast<std::int64_t>(completed_duration_ms.value()));
  state.checkpoints_written = checkpoints_written.value();
  state.frames_total = frames_total.value();
  state.frames_received = frames_received.value();
  state.frames_dropped = frames_dropped.value();
  state.timestamps.created_at = FromEpochMilliseconds(created_at_epoch_ms.value());
  state.timestamps.started_at = FromEpochMilliseconds(started_at_epoch_ms.value());
  state.timestamps.finished_at = FromEpochMilliseconds(finished_at_epoch_ms.value());
  state.updated_at = FromEpochMilliseconds(updated_at_epoch_ms.value());
  state.status = status;
  state.stop_reason = FindStringJsonField(text, "stop_reason").value_or("");
  return true;
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

bool AppendSoakFrameCache(const std::vector<backends::FrameSample>& frames,
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

bool LoadSoakFrameCache(const fs::path& frame_cache_path,
                        std::vector<backends::FrameSample>& frames, std::string& error) {
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

std::string ResolveSoakStopReason(const RunOptions& options) {
  if (g_soak_stop_requested.load()) {
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
  explicit ScopedInterruptSignalHandler(bool enabled) : enabled_(enabled) {
    if (!enabled_) {
      return;
    }
    g_soak_stop_requested.store(false);
    previous_handler_ = std::signal(SIGINT, HandleInterruptSignal);
  }

  ~ScopedInterruptSignalHandler() {
    if (!enabled_) {
      return;
    }
    (void)std::signal(SIGINT, previous_handler_);
  }

  ScopedInterruptSignalHandler(const ScopedInterruptSignalHandler&) = delete;
  ScopedInterruptSignalHandler& operator=(const ScopedInterruptSignalHandler&) = delete;

private:
  bool enabled_ = false;
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
  SoakCheckpointState resume_checkpoint;
  std::chrono::milliseconds completed_duration{0};
  std::vector<backends::FrameSample> frames;

  const auto created_at = std::chrono::system_clock::now();
  core::schema::RunInfo run_info = BuildRunInfo(options, run_plan, created_at);
  fs::path bundle_dir = ResolveExecutionOutputDir(options, run_info, use_per_run_bundle_dir);
  fs::path soak_frame_cache_path;
  fs::path soak_checkpoint_latest_path;
  fs::path soak_checkpoint_history_path;

  if (is_resume) {
    if (!LoadSoakCheckpoint(options.resume_checkpoint_path, resume_checkpoint, error)) {
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
    if (resume_checkpoint.status == SoakCheckpointStatus::kCompleted) {
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

    if (!LoadSoakFrameCache(soak_frame_cache_path, frames, error)) {
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

  if (!backend->Connect(error)) {
    logger.Error("backend connect failed",
                 {{"backend", run_info.config.backend}, {"error", error}});
    std::cerr << "error: backend connect failed: " << error << '\n';
    return kExitBackendConnectFailed;
  }
  logger.Info("backend connected", {{"backend", run_info.config.backend}});

  backends::BackendConfig applied_params;
  for (const auto& [key, value] : selected_device_params) {
    applied_params[key] = value;
  }
  if (!backends::sim::ApplyScenarioConfig(*backend, run_plan.sim_config, error, &applied_params)) {
    logger.Error("backend config apply failed", {{"error", error}});
    std::cerr << "error: backend config failed: " << error << '\n';
    return kExitFailure;
  }
  logger.Debug("backend config applied", {{"param_count", std::to_string(applied_params.size())}});

  fs::path events_path;
  const auto config_applied_at = std::chrono::system_clock::now();
  if (!AppendTraceEvent(events::EventType::kConfigApplied, config_applied_at,
                        BuildConfigAppliedPayload(run_info, applied_params), bundle_dir,
                        events_path, error)) {
    logger.Error("failed to append CONFIG_APPLIED event", {{"error", error}});
    std::cerr << "error: failed to append CONFIG_APPLIED event: " << error << '\n';
    return kExitFailure;
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
      payload["reason"] = "sim_fault_injection";
    }

    return AppendTraceEvent(dropped ? events::EventType::kFrameDropped
                                    : events::EventType::kFrameReceived,
                            frame.timestamp, std::move(payload), bundle_dir, events_path, error);
  };

  ScopedInterruptSignalHandler scoped_signal_handler(options.soak_mode);
  if (!options.soak_mode) {
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
    SoakCheckpointState checkpoint_state;
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
    checkpoint_state.status = SoakCheckpointStatus::kRunning;
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
          !AppendSoakFrameCache(normalized_chunk, soak_frame_cache_path, error)) {
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
      checkpoint_state.status = SoakCheckpointStatus::kRunning;
      checkpoint_state.stop_reason.clear();
      ++checkpoint_state.checkpoints_written;
      if (!WriteSoakCheckpointArtifacts(checkpoint_state, soak_checkpoint_latest_path,
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
        checkpoint_state.status = SoakCheckpointStatus::kPaused;
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
        if (!WriteSoakCheckpointArtifacts(checkpoint_state, soak_checkpoint_latest_path,
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
        std::vector<fs::path> bundle_artifact_paths = {
            scenario_artifact_path,      hostprobe_artifact_path,
            run_artifact_path,           events_path,
            soak_checkpoint_latest_path, soak_checkpoint_history_path,
            soak_frame_cache_path,
        };
        bundle_artifact_paths.insert(bundle_artifact_paths.end(),
                                     hostprobe_raw_artifact_paths.begin(),
                                     hostprobe_raw_artifact_paths.end());
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

    checkpoint_state.status = SoakCheckpointStatus::kCompleted;
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
    if (!WriteSoakCheckpointArtifacts(checkpoint_state, soak_checkpoint_latest_path,
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

  if (!AppendTraceEvent(events::EventType::kStreamStopped, run_info.timestamps.finished_at,
                        {
                            {"run_id", run_info.run_id},
                            {"frames_total", std::to_string(frames.size())},
                            {"frames_received", std::to_string(received_count)},
                            {"frames_dropped", std::to_string(dropped_count)},
                            {"reason", options.soak_mode ? "soak_completed" : "completed"},
                        },
                        bundle_dir, events_path, error)) {
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

  metrics::FpsReport fps_report;
  if (!metrics::ComputeFpsReport(frames, run_plan.duration, std::chrono::milliseconds(1'000),
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
  const bool thresholds_passed =
      EvaluateRunThresholds(run_plan.thresholds, fps_report, threshold_failures);
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
  std::vector<fs::path> bundle_artifact_paths = {
      scenario_artifact_path, hostprobe_artifact_path, run_artifact_path,     events_path,
      metrics_csv_path,       metrics_json_path,       summary_markdown_path, report_html_path,
  };
  if (options.soak_mode) {
    if (!soak_frame_cache_path.empty() && fs::exists(soak_frame_cache_path)) {
      bundle_artifact_paths.push_back(soak_frame_cache_path);
    }
    if (!soak_checkpoint_latest_path.empty() && fs::exists(soak_checkpoint_latest_path)) {
      bundle_artifact_paths.push_back(soak_checkpoint_latest_path);
    }
    if (!soak_checkpoint_history_path.empty() && fs::exists(soak_checkpoint_history_path)) {
      bundle_artifact_paths.push_back(soak_checkpoint_history_path);
    }
  }
  bundle_artifact_paths.insert(bundle_artifact_paths.end(), hostprobe_raw_artifact_paths.begin(),
                               hostprobe_raw_artifact_paths.end());
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

  logger.Info("run artifacts written", {{"bundle_dir", bundle_dir.string()},
                                        {"events", events_path.string()},
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
            << " rate_percent=" << fps_report.drop_rate_percent << '\n';
  std::cout << "timing_us: interval_avg=" << fps_report.inter_frame_interval_us.avg_us
            << " interval_p95=" << fps_report.inter_frame_interval_us.p95_us
            << " jitter_avg=" << fps_report.inter_frame_jitter_us.avg_us
            << " jitter_p95=" << fps_report.inter_frame_jitter_us.p95_us << '\n';
  std::cout << "frames: total=" << frames.size() << " received=" << received_count
            << " dropped=" << dropped_count << '\n';
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
