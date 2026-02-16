#ifndef LABOPS_TESTS_COMMON_TEMP_DIR_HPP_
#define LABOPS_TESTS_COMMON_TEMP_DIR_HPP_

#include "assertions.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace labops::tests::common {

inline std::filesystem::path CreateUniqueTempDir(std::string_view prefix) {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      (std::string(prefix) + "-" + std::to_string(now_ms));

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  if (ec) {
    Fail("failed to create temp root: " + root.string());
  }
  return root;
}

inline void RemovePathBestEffort(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

} // namespace labops::tests::common

#endif // LABOPS_TESTS_COMMON_TEMP_DIR_HPP_
