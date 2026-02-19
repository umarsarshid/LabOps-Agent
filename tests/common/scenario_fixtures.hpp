#ifndef LABOPS_TESTS_COMMON_SCENARIO_FIXTURES_HPP_
#define LABOPS_TESTS_COMMON_SCENARIO_FIXTURES_HPP_

#include "assertions.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace labops::tests::common {

// Scenario fixtures are checked into repo under /scenarios. Tests may execute
// from different working directories (IDE, CTest, CI), so lookup probes a
// small set of stable roots.
inline std::filesystem::path ResolveScenarioPath(std::string_view scenario_name) {
  const std::vector<std::filesystem::path> roots = {
      std::filesystem::current_path(),
      std::filesystem::current_path() / "..",
      std::filesystem::current_path() / "../..",
  };

  for (const auto& root : roots) {
    const std::filesystem::path candidate = root / "scenarios" / std::string(scenario_name);
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
      return candidate;
    }
  }

  return {};
}

inline std::filesystem::path RequireScenarioPath(std::string_view scenario_name) {
  const std::filesystem::path path = ResolveScenarioPath(scenario_name);
  if (path.empty()) {
    Fail("unable to resolve scenarios/" + std::string(scenario_name));
  }
  return path;
}

inline void WriteFixtureFile(const std::filesystem::path& file_path, std::string_view content) {
  std::error_code ec;
  if (!file_path.parent_path().empty()) {
    std::filesystem::create_directories(file_path.parent_path(), ec);
    if (ec) {
      Fail("failed to create fixture directory: " + file_path.parent_path().string());
    }
  }

  std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("failed to open fixture file for writing: " + file_path.string());
  }

  output << content;
  if (!output) {
    Fail("failed while writing fixture file: " + file_path.string());
  }
}

inline void WriteScenarioFixture(const std::filesystem::path& scenario_path,
                                 std::string_view scenario_json) {
  WriteFixtureFile(scenario_path, scenario_json);
}

} // namespace labops::tests::common

#endif // LABOPS_TESTS_COMMON_SCENARIO_FIXTURES_HPP_
