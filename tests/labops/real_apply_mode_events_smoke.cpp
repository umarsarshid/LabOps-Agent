#include "core/errors/exit_codes.hpp"
#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

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
    Fail("failed to set environment variable");
  }
#else
  if (setenv(name, value, 1) != 0) {
    Fail("failed to set environment variable");
  }
#endif
}

void UnsetEnvVar(const char* name) {
#if defined(_WIN32)
  if (_putenv_s(name, "") != 0) {
    Fail("failed to unset environment variable");
  }
#else
  if (unsetenv(name) != 0) {
    Fail("failed to unset environment variable");
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

int DispatchWithCapturedStderr(std::vector<std::string> argv_storage, std::string& stderr_text) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  std::ostringstream captured_cerr;
  std::streambuf* original_cerr = std::cerr.rdbuf(captured_cerr.rdbuf());
  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  std::cerr.rdbuf(original_cerr);

  stderr_text = captured_cerr.str();
  return exit_code;
}

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  std::vector<fs::path> bundles;
  if (!fs::exists(out_root)) {
    Fail("output root missing");
  }
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (entry.path().filename().string().rfind("run-", 0) == 0U) {
      bundles.push_back(entry.path());
    }
  }
  if (bundles.size() != 1U) {
    Fail("expected exactly one run bundle");
  }
  return bundles.front();
}

std::vector<std::string> ReadEventTypes(const fs::path& events_path) {
  std::ifstream input(events_path, std::ios::binary);
  if (!input) {
    Fail("failed to open events.jsonl");
  }

  std::vector<std::string> types;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t type_pos = line.find("\"type\":\"");
    if (type_pos == std::string::npos) {
      continue;
    }
    const std::size_t value_start = type_pos + 8U;
    const std::size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos) {
      continue;
    }
    types.push_back(line.substr(value_start, value_end - value_start));
  }
  return types;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open file for reading");
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool ContainsType(const std::vector<std::string>& types, std::string_view needle) {
  for (const std::string& type : types) {
    if (type == needle) {
      return true;
    }
  }
  return false;
}

void WriteScenario(const fs::path& path, std::string_view apply_mode,
                   std::string_view pixel_format) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    Fail("failed to open scenario path");
  }
  out << "{\n"
      << "  \"schema_version\": \"1.0\",\n"
      << "  \"scenario_id\": \"real_apply_mode_events\",\n"
      << "  \"backend\": \"real_stub\",\n"
      << "  \"apply_mode\": \"" << apply_mode << "\",\n"
      << "  \"duration\": { \"duration_ms\": 250 },\n"
      << "  \"camera\": {\n"
      << "    \"fps\": 1000,\n"
      << "    \"pixel_format\": \"" << pixel_format << "\"\n"
      << "  },\n"
      << "  \"thresholds\": {\n"
      << "    \"min_avg_fps\": 1.0\n"
      << "  }\n"
      << "}\n";
}

void WriteLimitedParamKeyMap(const fs::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    Fail("failed to open map override path");
  }
  out << "{\n"
      << "  \"frame_rate\": \"AcquisitionFrameRate\",\n"
      << "  \"pixel_format\": \"PixelFormat\"\n"
      << "}\n";
}

void AssertBestEffortRun(const fs::path& scenario_path, const fs::path& out_dir) {
  std::string stderr_text;
  const int exit_code = DispatchWithCapturedStderr(
      {"labops", "run", scenario_path.string(), "--out", out_dir.string()}, stderr_text);
  if (exit_code !=
      labops::core::errors::ToInt(labops::core::errors::ExitCode::kBackendConnectFailed)) {
    Fail("best-effort run should continue through config apply and then fail at connect");
  }

  const fs::path bundle = ResolveSingleBundleDir(out_dir);
  const fs::path events_path = bundle / "events.jsonl";
  if (!fs::exists(events_path)) {
    Fail("best-effort run should emit events.jsonl");
  }
  const std::vector<std::string> types = ReadEventTypes(events_path);
  if (!ContainsType(types, "CONFIG_APPLIED")) {
    Fail("best-effort run missing CONFIG_APPLIED event");
  }
  if (!ContainsType(types, "CONFIG_UNSUPPORTED")) {
    Fail("best-effort run missing CONFIG_UNSUPPORTED event");
  }
  if (!ContainsType(types, "CONFIG_ADJUSTED")) {
    Fail("best-effort run missing CONFIG_ADJUSTED event");
  }

  const fs::path verify_path = bundle / "config_verify.json";
  if (!fs::exists(verify_path)) {
    Fail("best-effort run missing config_verify.json");
  }
  const std::string verify_json = ReadFile(verify_path);
  if (verify_json.find("\"requested_count\"") == std::string::npos ||
      verify_json.find("\"supported_count\"") == std::string::npos ||
      verify_json.find("\"generic_key\":\"frame_rate\"") == std::string::npos ||
      verify_json.find("\"generic_key\":\"pixel_format\"") == std::string::npos ||
      verify_json.find("\"requested\":\"1000\"") == std::string::npos ||
      verify_json.find("\"actual\":\"240\"") == std::string::npos ||
      verify_json.find("allowed: mono8, mono12, rgb8") == std::string::npos ||
      verify_json.find("\"supported\":true") == std::string::npos) {
    Fail("best-effort config_verify.json missing pixel-format enum evidence");
  }

  const fs::path camera_config_path = bundle / "camera_config.json";
  if (!fs::exists(camera_config_path)) {
    Fail("best-effort run missing camera_config.json");
  }
  const std::string camera_config_json = ReadFile(camera_config_path);
  if (camera_config_json.find("\"curated_nodes\"") == std::string::npos ||
      camera_config_json.find("\"generic_key\":\"frame_rate\"") == std::string::npos ||
      camera_config_json.find("\"generic_key\":\"pixel_format\"") == std::string::npos ||
      camera_config_json.find("\"unsupported_keys\"") == std::string::npos) {
    Fail("best-effort camera_config.json missing curated node evidence");
  }

  const fs::path config_report_path = bundle / "config_report.md";
  if (!fs::exists(config_report_path)) {
    Fail("best-effort run missing config_report.md");
  }
  const std::string config_report = ReadFile(config_report_path);
  if (config_report.find("| Status | Key | Node | Requested | Actual | Notes |") ==
          std::string::npos ||
      config_report.find("✅ applied") == std::string::npos ||
      config_report.find("⚠ adjusted") == std::string::npos ||
      config_report.find("pixel_format") == std::string::npos ||
      config_report.find("allowed: mono8, mono12, rgb8") == std::string::npos ||
      config_report.find("❌ unsupported") == std::string::npos) {
    Fail("best-effort config_report.md missing pixel-format unsupported status evidence");
  }
}

void AssertStrictRun(const fs::path& scenario_path, const fs::path& out_dir) {
  std::string stderr_text;
  const int exit_code = DispatchWithCapturedStderr(
      {"labops", "run", scenario_path.string(), "--out", out_dir.string()}, stderr_text);
  if (exit_code != labops::core::errors::ToInt(labops::core::errors::ExitCode::kFailure)) {
    Fail("strict run should fail before backend connect when unsupported params exist");
  }

  const fs::path bundle = ResolveSingleBundleDir(out_dir);
  const fs::path events_path = bundle / "events.jsonl";
  if (!fs::exists(events_path)) {
    Fail("strict run should emit CONFIG_UNSUPPORTED evidence");
  }
  const std::vector<std::string> types = ReadEventTypes(events_path);
  if (!ContainsType(types, "CONFIG_UNSUPPORTED")) {
    Fail("strict run missing CONFIG_UNSUPPORTED event");
  }
  if (ContainsType(types, "CONFIG_APPLIED")) {
    Fail("strict run should not emit CONFIG_APPLIED when apply failed");
  }

  const fs::path verify_path = bundle / "config_verify.json";
  if (!fs::exists(verify_path)) {
    Fail("strict run missing config_verify.json");
  }
  const std::string verify_json = ReadFile(verify_path);
  if (verify_json.find("\"generic_key\":\"pixel_format\"") == std::string::npos ||
      verify_json.find("\"supported\":true") == std::string::npos ||
      verify_json.find("\"applied\":false") == std::string::npos) {
    Fail("strict config_verify.json missing pixel-format unsupported evidence");
  }

  const fs::path camera_config_path = bundle / "camera_config.json";
  if (!fs::exists(camera_config_path)) {
    Fail("strict run missing camera_config.json");
  }
  const std::string camera_config_json = ReadFile(camera_config_path);
  if (camera_config_json.find("\"unsupported_keys\"") == std::string::npos ||
      camera_config_json.find("\"pixel_format\"") == std::string::npos) {
    Fail("strict camera_config.json missing unsupported key evidence");
  }

  const fs::path config_report_path = bundle / "config_report.md";
  if (!fs::exists(config_report_path)) {
    Fail("strict run missing config_report.md");
  }
  const std::string config_report = ReadFile(config_report_path);
  if (config_report.find("❌ unsupported") == std::string::npos ||
      config_report.find("pixel_format") == std::string::npos ||
      config_report.find("allowed: mono8, mono12, rgb8") == std::string::npos) {
    Fail("strict config_report.md missing pixel-format unsupported status evidence");
  }
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-real-apply-mode-events-" + std::to_string(now_ms));
  const fs::path best_effort_scenario = temp_root / "best_effort.json";
  const fs::path strict_scenario = temp_root / "strict.json";
  const fs::path map_override = temp_root / "param_key_map.json";
  const fs::path out_best_effort = temp_root / "out_best_effort";
  const fs::path out_strict = temp_root / "out_strict";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  WriteScenario(best_effort_scenario, "best_effort", "yuv422");
  WriteScenario(strict_scenario, "strict", "yuv422");
  WriteLimitedParamKeyMap(map_override);

  const std::string map_override_text = map_override.string();
  ScopedEnvOverride map_override_scope("LABOPS_PARAM_KEY_MAP", map_override_text.c_str());

  AssertBestEffortRun(best_effort_scenario, out_best_effort);
  AssertStrictRun(strict_scenario, out_strict);

  fs::remove_all(temp_root, ec);
  return 0;
}
