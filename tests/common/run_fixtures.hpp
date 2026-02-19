#ifndef LABOPS_TESTS_COMMON_RUN_FIXTURES_HPP_
#define LABOPS_TESTS_COMMON_RUN_FIXTURES_HPP_

#include "assertions.hpp"
#include "cli_dispatch.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace labops::tests::common {

inline int DispatchRunScenario(const std::filesystem::path& scenario_path,
                               const std::filesystem::path& out_root,
                               const std::vector<std::string>& extra_args = {}) {
  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_root.string(),
  };
  argv_storage.insert(argv_storage.end(), extra_args.begin(), extra_args.end());
  return DispatchArgs(argv_storage);
}

inline void RunScenarioOrFail(const std::filesystem::path& scenario_path,
                              const std::filesystem::path& out_root,
                              const std::vector<std::string>& extra_args = {},
                              std::string_view context = "labops run returned non-zero exit code") {
  const int exit_code = DispatchRunScenario(scenario_path, out_root, extra_args);
  if (exit_code != 0) {
    Fail(std::string(context) + " (exit_code=" + std::to_string(exit_code) + ")");
  }
}

inline std::vector<std::filesystem::path> CollectRunBundleDirs(const std::filesystem::path& out_root) {
  if (!std::filesystem::exists(out_root)) {
    Fail("output root does not exist: " + out_root.string());
  }

  std::vector<std::filesystem::path> bundle_dirs;
  for (const auto& entry : std::filesystem::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  std::sort(bundle_dirs.begin(), bundle_dirs.end());
  return bundle_dirs;
}

inline std::filesystem::path RequireSingleRunBundleDir(const std::filesystem::path& out_root) {
  const std::vector<std::filesystem::path> bundle_dirs = CollectRunBundleDirs(out_root);
  if (bundle_dirs.size() != 1U) {
    Fail("expected exactly one run bundle directory under: " + out_root.string());
  }
  return bundle_dirs.front();
}

inline std::vector<std::filesystem::path> CollectFilesWithPrefixAndExtension(
    const std::filesystem::path& directory, std::string_view prefix, std::string_view extension) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind(std::string(prefix), 0) == 0U && entry.path().extension() == extension) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

inline std::size_t CountFilesWithPrefixAndExtension(const std::filesystem::path& directory,
                                                    std::string_view prefix,
                                                    std::string_view extension) {
  return CollectFilesWithPrefixAndExtension(directory, prefix, extension).size();
}

} // namespace labops::tests::common

#endif // LABOPS_TESTS_COMMON_RUN_FIXTURES_HPP_
