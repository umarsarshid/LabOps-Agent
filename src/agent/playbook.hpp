#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace labops::agent {

// One tunable variable the agent may change in order during triage.
// `rationale` keeps the ordering explainable to humans reading the plan.
struct PlaybookKnob {
  std::string name;
  std::string rationale;
};

// A playbook is an ordered, symptom-specific experiment plan.
// The order is important because we intentionally change one variable at a
// time and start with the most likely/high-signal levers.
struct Playbook {
  std::string id;
  std::string symptom;
  std::string objective;
  std::vector<PlaybookKnob> knobs;
};

// Returns a symptom-specific playbook when one exists.
//
// Contract:
// - true: `playbook` is populated, `error` is cleared.
// - false: `playbook` is reset and `error` explains why selection failed.
bool SelectPlaybookForSymptom(std::string_view symptom_input,
                              Playbook& playbook,
                              std::string& error);

} // namespace labops::agent
