#include "agent/variant_generator.hpp"

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

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to read file: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  const fs::path base_scenario_path = ResolveScenarioPath("dropped_frames.json");
  if (base_scenario_path.empty()) {
    Fail("unable to resolve scenarios/dropped_frames.json");
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path temp_root = fs::temp_directory_path() /
                             ("labops-oaat-variant-generator-" + std::to_string(now_ms));

  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  const fs::path original_cwd = fs::current_path(ec);
  if (ec) {
    Fail("failed to capture original cwd");
  }

  fs::current_path(temp_root, ec);
  if (ec) {
    Fail("failed to switch cwd");
  }

  labops::agent::VariantGenerationRequest request;
  request.base_scenario_path = base_scenario_path.string();
  request.symptom = "dropped_frames";
  // Intentionally rely on default output dir contract: out/agent_runs.

  labops::agent::VariantGenerationResult result;
  std::string error;
  labops::agent::OaatVariantGenerator generator;
  if (!generator.Generate(request, result, error)) {
    fs::current_path(original_cwd, ec);
    Fail("Generate failed: " + error);
  }

  const fs::path expected_output_dir = temp_root / "out" / "agent_runs";
  std::error_code compare_ec;
  const bool equivalent_paths =
      fs::exists(result.output_dir) && fs::exists(expected_output_dir) &&
      fs::equivalent(result.output_dir, expected_output_dir, compare_ec) && !compare_ec;
  if (!equivalent_paths) {
    fs::current_path(original_cwd, ec);
    Fail("variants were not generated under default out/agent_runs path: got '" +
         result.output_dir.string() + "', expected '" + expected_output_dir.string() + "'");
  }

  if (!fs::exists(result.output_dir) || !fs::is_directory(result.output_dir)) {
    fs::current_path(original_cwd, ec);
    Fail("variant output directory missing");
  }

  if (result.variants.size() != 5U) {
    fs::current_path(original_cwd, ec);
    Fail("expected 5 one-knob variants for dropped_frames playbook");
  }

  const std::string expected_base_name = "dropped_frames";
  for (const auto& variant : result.variants) {
    if (!fs::exists(variant.scenario_path) || !fs::is_regular_file(variant.scenario_path)) {
      fs::current_path(original_cwd, ec);
      Fail("missing generated variant scenario: " + variant.scenario_path.string());
    }

    const std::string file_name = variant.scenario_path.filename().string();
    AssertContains(file_name, expected_base_name);
    AssertContains(file_name, variant.knob_name);

    const std::string scenario_text = ReadFile(variant.scenario_path);
    AssertContains(scenario_text, "\"scenario_id\"");
    AssertContains(scenario_text, variant.knob_path.substr(0, variant.knob_path.find('.')));
  }

  if (!fs::exists(result.manifest_path) || !fs::is_regular_file(result.manifest_path)) {
    fs::current_path(original_cwd, ec);
    Fail("variants_manifest.json missing");
  }

  const std::string manifest_text = ReadFile(result.manifest_path);
  AssertContains(manifest_text, "\"playbook_id\":\"dropped_frames_oaat_v1\"");
  AssertContains(manifest_text, "\"knob_name\":\"packet_delay_ms\"");
  AssertContains(manifest_text, "\"knob_name\":\"fps\"");
  AssertContains(manifest_text, "\"knob_name\":\"roi_enabled\"");
  AssertContains(manifest_text, "\"knob_name\":\"reorder_percent\"");
  AssertContains(manifest_text, "\"knob_name\":\"loss_percent\"");

  fs::current_path(original_cwd, ec);
  fs::remove_all(temp_root, ec);
  std::cout << "oaat_variant_generator_smoke: ok\n";
  return 0;
}
