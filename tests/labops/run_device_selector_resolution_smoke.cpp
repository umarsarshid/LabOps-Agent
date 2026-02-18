#include "backends/real_sdk/real_backend_factory.hpp"
#include "core/errors/exit_codes.hpp"
#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected to find: " << needle << '\n';
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
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

void WriteFixtureCsv(const fs::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    Fail("failed to open fixture output file");
  }
  out << "model,serial,user_id,transport,ip,mac,firmware_version,sdk_version\n";
  out << "SprintCam,SN-1001,Primary,GigE,10.0.0.21,aa-bb-cc-dd-ee-01,3.2.1,21.1.8\n";
  out << "SprintCam,SN-2000,Secondary,USB3VISION,,,4.0.0,21.1.8\n";
}

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  if (!fs::exists(out_root)) {
    Fail("output root does not exist");
  }

  std::vector<fs::path> bundle_dirs;
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  if (bundle_dirs.size() != 1U) {
    Fail("expected exactly one run bundle directory");
  }
  return bundle_dirs.front();
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open file");
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

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

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root =
      fs::temp_directory_path() / ("labops-run-device-selector-smoke-" + std::to_string(now_ms));
  const fs::path scenario_path = temp_root / "real_selector_run.json";
  const fs::path fixture_path = temp_root / "devices.csv";
  const fs::path out_usb_dir = temp_root / "out_usb";
  const fs::path out_gige_dir = temp_root / "out_gige";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    if (!scenario_file) {
      Fail("failed to open scenario file");
    }
    scenario_file << "{\n"
                  << "  \"schema_version\": \"1.0\",\n"
                  << "  \"scenario_id\": \"run_device_selector_smoke\",\n"
                  << "  \"backend\": \"real_stub\",\n"
                  << "  \"device_selector\": \"serial:SN-1001\",\n"
                  << "  \"duration\": {\n"
                  << "    \"duration_ms\": 500\n"
                  << "  },\n"
                  << "  \"camera\": {\n"
                  << "    \"fps\": 30,\n"
                  << "    \"exposure_us\": 8000,\n"
                  << "    \"gain_db\": 6.5,\n"
                  << "    \"trigger_mode\": \"hardware\",\n"
                  << "    \"trigger_source\": \"line1\",\n"
                  << "    \"trigger_activation\": \"falling_edge\",\n"
                  << "    \"roi\": {\n"
                  << "      \"x\": 100,\n"
                  << "      \"y\": 120,\n"
                  << "      \"width\": 1280,\n"
                  << "      \"height\": 720\n"
                  << "    },\n"
                  << "    \"network\": {\n"
                  << "      \"packet_size_bytes\": 9000,\n"
                  << "      \"inter_packet_delay_us\": 200\n"
                  << "    }\n"
                  << "  },\n"
                  << "  \"thresholds\": {\n"
                  << "    \"min_avg_fps\": 1.0\n"
                  << "  }\n"
                  << "}\n";
  }

  WriteFixtureCsv(fixture_path);
  const std::string fixture_path_text = fixture_path.string();
  ScopedEnvOverride fixture_override("LABOPS_REAL_DEVICE_FIXTURE", fixture_path_text.c_str());

  std::string stderr_output;
  const int usb_exit_code =
      DispatchWithCapturedStderr({"labops", "run", scenario_path.string(), "--out",
                                  out_usb_dir.string(), "--device", "serial:SN-2000"},
                                 stderr_output);

  if (labops::backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    if (usb_exit_code !=
        labops::core::errors::ToInt(labops::core::errors::ExitCode::kBackendConnectFailed)) {
      Fail("expected backend-connect-failed exit code in real-enabled build");
    }
    AssertContains(stderr_output, "msg=\"device selector resolved\"");
    AssertContains(stderr_output, "selector=\"serial:SN-2000\"");
    AssertContains(stderr_output, "selected_serial=\"SN-2000\"");
    AssertContains(stderr_output, "selected_firmware_version=\"4.0.0\"");
    AssertContains(stderr_output, "selected_sdk_version=\"21.1.8\"");

    const fs::path bundle_dir = ResolveSingleBundleDir(out_usb_dir);
    const fs::path run_json_path = bundle_dir / "run.json";
    const fs::path config_verify_path = bundle_dir / "config_verify.json";
    const fs::path camera_config_path = bundle_dir / "camera_config.json";
    const fs::path config_report_path = bundle_dir / "config_report.md";
    if (!fs::exists(run_json_path)) {
      Fail("expected run.json to be written on backend connect failure");
    }
    if (!fs::exists(config_verify_path)) {
      Fail("expected config_verify.json to be written before backend connect failure");
    }
    if (!fs::exists(camera_config_path)) {
      Fail("expected camera_config.json to be written before backend connect failure");
    }
    if (!fs::exists(config_report_path)) {
      Fail("expected config_report.md to be written before backend connect failure");
    }
    const std::string run_json = ReadFile(run_json_path);
    AssertContains(run_json, "\"real_device\":");
    AssertContains(run_json, "\"model\":\"SprintCam\"");
    AssertContains(run_json, "\"serial\":\"SN-2000\"");
    AssertContains(run_json, "\"transport\":\"usb\"");
    AssertContains(run_json, "\"firmware_version\":\"4.0.0\"");
    AssertContains(run_json, "\"sdk_version\":\"21.1.8\"");

    const std::string verify_json = ReadFile(config_verify_path);
    AssertContains(verify_json, "\"requested_count\"");
    AssertContains(verify_json, "\"generic_key\":\"frame_rate\"");
    AssertContains(verify_json, "\"generic_key\":\"exposure\"");
    AssertContains(verify_json, "\"generic_key\":\"gain\"");
    AssertContains(verify_json, "\"generic_key\":\"trigger_mode\"");
    AssertContains(verify_json, "\"generic_key\":\"trigger_source\"");
    AssertContains(verify_json, "\"generic_key\":\"trigger_activation\"");
    AssertContains(verify_json, "\"generic_key\":\"roi_width\"");
    AssertContains(verify_json, "\"generic_key\":\"roi_height\"");
    AssertContains(verify_json, "\"generic_key\":\"roi_offset_x\"");
    AssertContains(verify_json, "\"generic_key\":\"roi_offset_y\"");
    AssertContains(verify_json, "\"actual\":\"8000\"");
    AssertContains(verify_json, "\"actual\":\"6.5\"");
    AssertContains(verify_json, "\"actual\":\"hardware\"");
    AssertContains(verify_json, "\"actual\":\"line1\"");
    AssertContains(verify_json, "\"actual\":\"falling_edge\"");
    AssertContains(verify_json, "\"actual\":\"1280\"");
    AssertContains(verify_json, "\"actual\":\"720\"");
    AssertContains(verify_json, "\"actual\":\"100\"");
    AssertContains(verify_json, "\"actual\":\"120\"");
    AssertContains(verify_json, "\"generic_key\":\"packet_size_bytes\"");
    AssertContains(verify_json, "\"generic_key\":\"inter_packet_delay_us\"");
    AssertContains(verify_json, "requires GigE transport (resolved transport: usb)");
    AssertContains(verify_json, "\"supported\":true");

    const std::string camera_config_json = ReadFile(camera_config_path);
    AssertContains(camera_config_json, "\"identity\":{");
    AssertContains(camera_config_json, "\"model\":\"SprintCam\"");
    AssertContains(camera_config_json, "\"serial\":\"SN-2000\"");
    AssertContains(camera_config_json, "\"selector\":\"serial:SN-2000\"");
    AssertContains(camera_config_json, "\"curated_nodes\":");
    AssertContains(camera_config_json, "\"missing_keys\":");
    AssertContains(camera_config_json, "\"unsupported_keys\":");

    const std::string config_report = ReadFile(config_report_path);
    AssertContains(config_report, "| Status | Key | Node | Requested | Actual | Notes |");
    AssertContains(config_report, "frame_rate");
    AssertContains(config_report, "exposure");
    AssertContains(config_report, "gain");
    AssertContains(config_report, "trigger_mode");
    AssertContains(config_report, "trigger_source");
    AssertContains(config_report, "trigger_activation");
    AssertContains(config_report, "roi_width");
    AssertContains(config_report, "roi_height");
    AssertContains(config_report, "roi_offset_x");
    AssertContains(config_report, "roi_offset_y");
    AssertContains(config_report, "packet_size_bytes");
    AssertContains(config_report, "inter_packet_delay_us");
    AssertContains(config_report, "requires GigE transport (resolved transport: usb)");
    AssertContains(config_report, "units: us; validated range: [5, 10000000]");
    AssertContains(config_report, "units: dB; validated range: [0, 48]");
    AssertContains(config_report, "units: bytes; GigE-only; validated range: [576, 9000]");
    AssertContains(config_report, "units: us; GigE-only; validated range: [0, 100000]");
    AssertContains(config_report, "units: px; validated range: [64, 4096]; applied before offsets");
    AssertContains(config_report,
                   "units: px; validated range: [0, 4095]; applied after width/height");
    AssertContains(config_report, "❌ unsupported");

    std::string gige_stderr_output;
    const int gige_exit_code =
        DispatchWithCapturedStderr({"labops", "run", scenario_path.string(), "--out",
                                    out_gige_dir.string(), "--device", "serial:SN-1001"},
                                   gige_stderr_output);
    if (gige_exit_code !=
        labops::core::errors::ToInt(labops::core::errors::ExitCode::kBackendConnectFailed)) {
      Fail("expected backend-connect-failed exit code for GigE selector run");
    }
    AssertContains(gige_stderr_output, "selected_serial=\"SN-1001\"");
    AssertContains(gige_stderr_output, "selected_transport=\"gige\"");

    const fs::path gige_bundle_dir = ResolveSingleBundleDir(out_gige_dir);
    const std::string gige_run_json = ReadFile(gige_bundle_dir / "run.json");
    AssertContains(gige_run_json, "\"serial\":\"SN-1001\"");
    AssertContains(gige_run_json, "\"transport\":\"gige\"");

    const std::string gige_verify_json = ReadFile(gige_bundle_dir / "config_verify.json");
    AssertContains(gige_verify_json, "\"generic_key\":\"packet_size_bytes\"");
    AssertContains(gige_verify_json, "\"generic_key\":\"inter_packet_delay_us\"");
    AssertContains(gige_verify_json, "\"actual\":\"9000\"");
    AssertContains(gige_verify_json, "\"actual\":\"200\"");
    AssertContains(gige_verify_json, "\"supported\":true");
    AssertContains(gige_verify_json, "\"applied\":true");

    const std::string gige_config_report = ReadFile(gige_bundle_dir / "config_report.md");
    AssertContains(gige_config_report, "packet_size_bytes");
    AssertContains(gige_config_report, "inter_packet_delay_us");
    AssertContains(gige_config_report, "✅ applied");
  } else {
    if (usb_exit_code != labops::core::errors::ToInt(labops::core::errors::ExitCode::kFailure)) {
      Fail("expected generic failure exit code when real backend is disabled");
    }
    AssertContains(stderr_output, "device selector resolution failed");
    AssertContains(stderr_output, "real backend");
  }

  fs::remove_all(temp_root, ec);
  return 0;
}
