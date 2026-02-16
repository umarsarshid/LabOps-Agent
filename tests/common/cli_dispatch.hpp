#ifndef LABOPS_TESTS_COMMON_CLI_DISPATCH_HPP_
#define LABOPS_TESTS_COMMON_CLI_DISPATCH_HPP_

#include "labops/cli/router.hpp"

#include <string>
#include <vector>

namespace labops::tests::common {

inline int DispatchArgs(const std::vector<std::string>& argv_storage) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (const auto& arg : argv_storage) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
}

} // namespace labops::tests::common

#endif // LABOPS_TESTS_COMMON_CLI_DISPATCH_HPP_
