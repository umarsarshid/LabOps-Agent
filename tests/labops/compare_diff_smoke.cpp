#include "labops/cli/router.hpp"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

fs::path ResolveScenarioPath(const std::string& scenario_name) {
  const std::vector<fs::path> roots = {
      fs::current_path(),
      fs::current_path() / "..",
      fs::current_path() / "../..",
  };

  for (const auto& root : roots) {
    const fs::path candidate = root / "scenarios" / scenario_name;
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
      return candidate;
    }
  }

  return {};
}

int DispatchArgs(const std::vector<std::string>& argv_storage) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (const auto& arg : argv_storage) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
}

fs::path ResolveSingleRunBundleDir(const fs::path& out_root) {
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

double ExtractDeltaForMetricFromDiffJson(const std::string& diff_json,
                                         const std::string& metric_name) {
  const std::string metric_token = "\"metric\":\"" + metric_name + "\"";
  const std::size_t metric_pos = diff_json.find(metric_token);
  if (metric_pos == std::string::npos) {
    Fail("failed to locate metric in diff json: " + metric_name);
  }

  const std::size_t delta_key_pos = diff_json.find("\"delta\":", metric_pos);
  if (delta_key_pos == std::string::npos) {
    Fail("failed to locate delta field in diff json");
  }

  std::size_t value_pos = delta_key_pos + std::string_view("\"delta\":").size();
  while (value_pos < diff_json.size() &&
         (diff_json[value_pos] == ' ' || diff_json[value_pos] == '\n' || diff_json[value_pos] == '\t')) {
    ++value_pos;
  }

  std::size_t value_end = value_pos;
  while (value_end < diff_json.size()) {
    const char ch = diff_json[value_end];
    const bool numeric_char =
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E';
    if (!numeric_char) {
      break;
    }
    ++value_end;
  }

  if (value_end == value_pos) {
    Fail("delta value parse failed for metric: " + metric_name);
  }

  const std::string value_text = diff_json.substr(value_pos, value_end - value_pos);
  try {
    return std::stod(value_text);
  } catch (...) {
    Fail("failed to convert delta value to double for metric: " + metric_name);
  }

  return 0.0;
}

} // namespace

int main() {
  const fs::path baseline_scenario_path = ResolveScenarioPath("sim_baseline.json");
  if (baseline_scenario_path.empty()) {
    Fail("unable to resolve scenarios/sim_baseline.json");
  }

  const fs::path run_scenario_path = ResolveScenarioPath("dropped_frames.json");
  if (run_scenario_path.empty()) {
    Fail("unable to resolve scenarios/dropped_frames.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() / ("labops-compare-diff-" + std::to_string(now_ms));
  const fs::path out_dir = temp_root / "out";

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  const fs::path original_cwd = fs::current_path(ec);
  if (ec) {
    Fail("failed to resolve original cwd");
  }

  fs::current_path(temp_root, ec);
  if (ec) {
    Fail("failed to switch cwd for compare diff smoke");
  }

  if (DispatchArgs({"labops", "baseline", "capture", baseline_scenario_path.string()}) != 0) {
    fs::current_path(original_cwd, ec);
    Fail("baseline capture command failed");
  }

  if (DispatchArgs({"labops", "run", run_scenario_path.string(), "--out", out_dir.string()}) != 0) {
    fs::current_path(original_cwd, ec);
    Fail("run command failed");
  }

  const fs::path run_bundle_dir = ResolveSingleRunBundleDir(out_dir);
  const fs::path baseline_dir = temp_root / "baselines" / "sim_baseline";

  if (DispatchArgs({"labops", "compare", "--baseline", baseline_dir.string(), "--run", run_bundle_dir.string()}) != 0) {
    fs::current_path(original_cwd, ec);
    Fail("compare command failed");
  }

  const fs::path diff_json_path = run_bundle_dir / "diff.json";
  const fs::path diff_md_path = run_bundle_dir / "diff.md";
  if (!fs::exists(diff_json_path)) {
    fs::current_path(original_cwd, ec);
    Fail("compare did not produce diff.json");
  }
  if (!fs::exists(diff_md_path)) {
    fs::current_path(original_cwd, ec);
    Fail("compare did not produce diff.md");
  }

  std::ifstream diff_json_input(diff_json_path, std::ios::binary);
  if (!diff_json_input) {
    fs::current_path(original_cwd, ec);
    Fail("failed to open diff.json");
  }
  const std::string diff_json((std::istreambuf_iterator<char>(diff_json_input)),
                              std::istreambuf_iterator<char>());
  AssertContains(diff_json, "\"compared_metrics\":[");
  AssertContains(diff_json, "\"metric\":\"avg_fps\"");
  AssertContains(diff_json, "\"metric\":\"drop_rate_percent\"");

  const double avg_fps_delta = ExtractDeltaForMetricFromDiffJson(diff_json, "avg_fps");
  const double drop_rate_delta = ExtractDeltaForMetricFromDiffJson(diff_json, "drop_rate_percent");
  if (std::abs(avg_fps_delta) <= 1e-9) {
    fs::current_path(original_cwd, ec);
    Fail("expected non-zero avg_fps delta");
  }
  if (std::abs(drop_rate_delta) <= 1e-9) {
    fs::current_path(original_cwd, ec);
    Fail("expected non-zero drop_rate_percent delta");
  }

  std::ifstream diff_md_input(diff_md_path, std::ios::binary);
  if (!diff_md_input) {
    fs::current_path(original_cwd, ec);
    Fail("failed to open diff.md");
  }
  const std::string diff_md((std::istreambuf_iterator<char>(diff_md_input)),
                            std::istreambuf_iterator<char>());
  AssertContains(diff_md, "# Metrics Diff");
  AssertContains(diff_md, "| drop_rate_percent |");

  fs::current_path(original_cwd, ec);
  fs::remove_all(temp_root, ec);
  std::cout << "compare_diff_smoke: ok\n";
  return 0;
}
