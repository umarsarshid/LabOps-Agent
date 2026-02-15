#include "backends/real_sdk/real_backend_factory.hpp"
#include "labops/cli/router.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

int DispatchWithCapturedStreams(std::vector<std::string> argv_storage, std::string& stdout_text,
                                std::string& stderr_text) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  std::ostringstream captured_stdout;
  std::ostringstream captured_stderr;
  std::streambuf* original_cout = std::cout.rdbuf(captured_stdout.rdbuf());
  std::streambuf* original_cerr = std::cerr.rdbuf(captured_stderr.rdbuf());
  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  std::cout.rdbuf(original_cout);
  std::cerr.rdbuf(original_cerr);

  stdout_text = captured_stdout.str();
  stderr_text = captured_stderr.str();
  return exit_code;
}

} // namespace

int main() {
  std::string stdout_text;
  std::string stderr_text;
  const int exit_code = DispatchWithCapturedStreams({"labops", "list-devices", "--backend", "real"},
                                                    stdout_text, stderr_text);

  if (labops::backends::real_sdk::IsRealBackendEnabledAtBuild()) {
    if (exit_code != 0) {
      Fail("list-devices should succeed when real backend is enabled");
    }
    AssertContains(stdout_text, "backend: real");
    AssertContains(stdout_text, "status: enabled");
    AssertContains(stdout_text, "devices: 0");
    AssertContains(stdout_text, "real backend skeleton does not implement device discovery yet");
    return 0;
  }

  if (exit_code == 0) {
    Fail("list-devices should fail when real backend is not available");
  }
  AssertContains(stderr_text, "BACKEND_NOT_AVAILABLE");
  AssertContains(stderr_text,
                 std::string("real backend ") +
                     std::string(labops::backends::real_sdk::RealBackendAvailabilityStatusText()));
  return 0;
}
