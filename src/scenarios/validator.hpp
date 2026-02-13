#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace labops::scenarios {

struct ValidationIssue {
  std::string path;
  std::string message;
};

struct ValidationReport {
  bool valid = false;
  std::vector<ValidationIssue> issues;
};

// Validates scenario JSON text against the current schema contract.
//
// Contract:
// - Returns true when validation completed (even if schema is invalid).
// - Returns false only for internal failures outside schema validation flow.
// - Populates `report.valid` and `report.issues`.
// - On parse errors, emits actionable issue(s) under path `$`.
bool ValidateScenarioText(std::string_view json_text, ValidationReport& report, std::string& error);

// Loads and validates a scenario file.
//
// Contract:
// - Returns false if file I/O fails and sets `error`.
// - Otherwise returns true and populates `report`.
bool ValidateScenarioFile(const std::string& scenario_path, ValidationReport& report,
                          std::string& error);

} // namespace labops::scenarios
