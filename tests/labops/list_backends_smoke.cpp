#include "backends/webcam/webcam_factory.hpp"
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
    std::cerr << "expected output to contain: " << needle << '\n';
    std::cerr << "actual output:\n" << text << '\n';
    std::abort();
  }
}

int DispatchWithCapturedStdout(std::vector<std::string> argv_storage, std::string& stdout_text) {
  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  std::ostringstream captured_cout;
  std::streambuf* original_cout = std::cout.rdbuf(captured_cout.rdbuf());
  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  std::cout.rdbuf(original_cout);

  stdout_text = captured_cout.str();
  return exit_code;
}

} // namespace

int main() {
  std::string stdout_text;
  const int exit_code = DispatchWithCapturedStdout({"labops", "list-backends"}, stdout_text);
  if (exit_code != 0) {
    Fail("list-backends returned non-zero exit code");
  }

  AssertContains(stdout_text, "sim ");
  AssertContains(stdout_text, "webcam ");
  AssertContains(stdout_text, "real ");

  const labops::backends::webcam::WebcamBackendAvailability webcam_availability =
      labops::backends::webcam::GetWebcamBackendAvailability();
  if (webcam_availability.available) {
    AssertContains(stdout_text, "webcam ✅ enabled");
  } else {
    AssertContains(stdout_text, "webcam ⚠️ disabled (");
    AssertContains(stdout_text, webcam_availability.reason);
  }

  const bool has_real_enabled = stdout_text.find("real ✅ enabled") != std::string::npos;
  const bool has_real_sdk_missing =
      stdout_text.find("real ⚠️ disabled (SDK not found)") != std::string::npos;
  const bool has_real_build_off =
      stdout_text.find("real ⚠️ disabled (build option OFF)") != std::string::npos;

  if (!has_real_enabled && !has_real_sdk_missing && !has_real_build_off) {
    std::cerr << "unexpected real backend status output:\n" << stdout_text << '\n';
    return 1;
  }

  std::cout << "list_backends_smoke: ok\n";
  return 0;
}
