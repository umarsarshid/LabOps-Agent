#ifndef LABOPS_TESTS_COMMON_ASSERTIONS_HPP_
#define LABOPS_TESTS_COMMON_ASSERTIONS_HPP_

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace labops::tests::common {

[[noreturn]] inline void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

inline void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) != std::string_view::npos) {
    return;
  }
  std::cerr << "expected to find: " << needle << '\n';
  std::cerr << "actual text: " << text << '\n';
  std::abort();
}

inline void AssertNotContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    return;
  }
  std::cerr << "expected to not find: " << needle << '\n';
  std::cerr << "actual text: " << text << '\n';
  std::abort();
}

inline std::string ReadFileToString(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("failed to open file: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace labops::tests::common

#endif // LABOPS_TESTS_COMMON_ASSERTIONS_HPP_
