#include "../common/assertions.hpp"
#include "../common/scenario_fixtures.hpp"
#include "../common/temp_dir.hpp"
#include "agent/variant_generator.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

using labops::tests::common::AssertContains;
using labops::tests::common::CreateUniqueTempDir;
using labops::tests::common::Fail;
using labops::tests::common::ReadFileToString;
using labops::tests::common::RequireScenarioPath;

} // namespace

int main() {
  const fs::path base_scenario_path = RequireScenarioPath("dropped_frames.json");
  const fs::path temp_root = CreateUniqueTempDir("labops-oaat-variant-generator");

  std::error_code ec;

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

    const std::string scenario_text = ReadFileToString(variant.scenario_path);
    AssertContains(scenario_text, "\"scenario_id\"");
    AssertContains(scenario_text, variant.knob_path.substr(0, variant.knob_path.find('.')));
  }

  if (!fs::exists(result.manifest_path) || !fs::is_regular_file(result.manifest_path)) {
    fs::current_path(original_cwd, ec);
    Fail("variants_manifest.json missing");
  }

  const std::string manifest_text = ReadFileToString(result.manifest_path);
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
