#include "labops/cli/router.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertRange(double value, double min_inclusive, double max_inclusive, std::string_view name) {
  if (value < min_inclusive || value > max_inclusive) {
    std::cerr << "range assertion failed for " << name << ": value=" << value
              << " expected=[" << min_inclusive << ", " << max_inclusive << "]\n";
    std::abort();
  }
}

std::optional<double> ParseNumberAfterKey(std::string_view text, std::string_view key,
                                          std::size_t search_start = 0U) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t key_pos = text.find(needle, search_start);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t value_start = key_pos + needle.size();
  while (value_start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[value_start])) != 0) {
    ++value_start;
  }
  if (value_start >= text.size()) {
    return std::nullopt;
  }

  // Numeric values in metrics.json are emitted as plain JSON numbers.
  if (text[value_start] == '-' || text[value_start] == '+' ||
      std::isdigit(static_cast<unsigned char>(text[value_start])) != 0) {
    std::size_t value_end = value_start + 1U;
    while (value_end < text.size()) {
      const char c = text[value_end];
      const bool numeric_char = std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.' ||
                                c == 'e' || c == 'E' || c == '-' || c == '+';
      if (!numeric_char) {
        break;
      }
      ++value_end;
    }

    const std::string token(text.substr(value_start, value_end - value_start));
    try {
      std::size_t parsed = 0;
      const double value = std::stod(token, &parsed);
      if (parsed != token.size()) {
        return std::nullopt;
      }
      return value;
    } catch (...) {
      return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<double> ParseNestedNumber(std::string_view text, std::string_view object_key,
                                        std::string_view field_key) {
  const std::string object_needle = "\"" + std::string(object_key) + "\":{";
  const std::size_t object_pos = text.find(object_needle);
  if (object_pos == std::string_view::npos) {
    return std::nullopt;
  }
  return ParseNumberAfterKey(text, field_key, object_pos + object_needle.size());
}

fs::path ResolveBaselineScenarioPath() {
  const std::vector<fs::path> roots = {
      fs::current_path(),
      fs::current_path() / "..",
      fs::current_path() / "../..",
  };

  for (const auto& root : roots) {
    const fs::path candidate = root / "scenarios" / "sim_baseline.json";
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
      return candidate;
    }
  }
  return {};
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

} // namespace

int main() {
  const fs::path scenario_path = ResolveBaselineScenarioPath();
  if (scenario_path.empty()) {
    Fail("unable to resolve scenarios/sim_baseline.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path out_dir = fs::temp_directory_path() / ("labops-baseline-metrics-" + std::to_string(now_ms));
  std::error_code cleanup_ec;
  fs::remove_all(out_dir, cleanup_ec);

  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_dir.string(),
  };
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  if (exit_code != 0) {
    Fail("labops run failed for scenarios/sim_baseline.json");
  }

  const fs::path bundle_dir = ResolveSingleBundleDir(out_dir);
  const fs::path bundle_manifest_path = bundle_dir / "bundle_manifest.json";
  const fs::path metrics_json_path = bundle_dir / "metrics.json";
  if (!fs::exists(bundle_manifest_path)) {
    Fail("bundle_manifest.json was not generated for baseline scenario");
  }
  if (!fs::exists(metrics_json_path)) {
    Fail("metrics.json was not generated for baseline scenario");
  }

  std::ifstream input(metrics_json_path, std::ios::binary);
  if (!input) {
    Fail("failed to open metrics.json");
  }
  const std::string metrics_json((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());

  const auto avg_fps = ParseNumberAfterKey(metrics_json, "avg_fps");
  const auto drop_rate_percent = ParseNumberAfterKey(metrics_json, "drop_rate_percent");
  const auto frames_total = ParseNumberAfterKey(metrics_json, "frames_total");
  const auto dropped_total = ParseNumberAfterKey(metrics_json, "dropped_frames_total");
  const auto interval_p95 = ParseNestedNumber(metrics_json, "inter_frame_interval_us", "p95_us");
  const auto jitter_p95 = ParseNestedNumber(metrics_json, "inter_frame_jitter_us", "p95_us");

  if (!avg_fps.has_value() || !drop_rate_percent.has_value() || !frames_total.has_value() ||
      !dropped_total.has_value() || !interval_p95.has_value() || !jitter_p95.has_value()) {
    Fail("metrics.json missing one or more required numeric fields");
  }

  // Baseline scenario expectations:
  // - 10 seconds @ 30 FPS should stay very close to 30 average.
  // - No injected drops => zero drop rate/total.
  // - Inter-frame timing should stay near frame period (~33.3 ms).
  // - Jitter p95 should remain low in baseline mode.
  AssertRange(avg_fps.value(), 29.5, 30.5, "avg_fps");
  AssertRange(drop_rate_percent.value(), 0.0, 0.001, "drop_rate_percent");
  AssertRange(dropped_total.value(), 0.0, 0.001, "dropped_frames_total");
  AssertRange(frames_total.value(), 295.0, 305.0, "frames_total");
  AssertRange(interval_p95.value(), 30000.0, 36000.0, "inter_frame_interval_us.p95_us");
  AssertRange(jitter_p95.value(), 0.0, 1000.0, "inter_frame_jitter_us.p95_us");

  fs::remove_all(out_dir, cleanup_ec);
  std::cout << "sim_baseline_metrics_integration_smoke: ok\n";
  return 0;
}
