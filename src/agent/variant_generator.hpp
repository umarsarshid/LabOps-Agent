#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace labops::agent {

// One generated scenario variant where exactly one playbook knob is mutated
// relative to the base scenario configuration.
struct ScenarioVariant {
  std::string knob_name;
  std::string knob_path;
  std::string before_value;
  std::string after_value;
  std::filesystem::path scenario_path;
};

// Request contract for one-variable-at-a-time scenario generation.
struct VariantGenerationRequest {
  std::string base_scenario_path;
  std::string symptom;
  std::filesystem::path output_dir = std::filesystem::path("out") / "agent_runs";
};

// Result contract exposed to agent orchestrators.
struct VariantGenerationResult {
  std::string playbook_id;
  std::filesystem::path output_dir;
  std::filesystem::path manifest_path;
  std::vector<ScenarioVariant> variants;
};

// Generates deterministic scenario variants where each output differs by one
// knob from the base scenario.
class OaatVariantGenerator {
public:
  bool Generate(const VariantGenerationRequest& request,
                VariantGenerationResult& result,
                std::string& error) const;
};

} // namespace labops::agent
