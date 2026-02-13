#include "labops/cli/router.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

fs::path ResolveSingleBundleDir(const fs::path& out_root) {
  if (!fs::exists(out_root)) {
    Fail("output root does not exist");
  }

  std::vector<fs::path> bundle_dirs;
  for (const auto& entry : fs::directory_iterator(out_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.rfind("run-", 0) == 0U) {
      bundle_dirs.push_back(entry.path());
    }
  }

  if (bundle_dirs.size() != 1U) {
    Fail("expected exactly one run bundle directory");
  }
  return bundle_dirs.front();
}

bool RunScenario(const fs::path& scenario_path, const fs::path& out_root, bool with_zip) {
  std::vector<std::string> argv_storage = {
      "labops",
      "run",
      scenario_path.string(),
      "--out",
      out_root.string(),
  };
  if (with_zip) {
    argv_storage.push_back("--zip");
  }

  std::vector<char*> argv;
  argv.reserve(argv_storage.size());
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }

  const int exit_code = labops::cli::Dispatch(static_cast<int>(argv.size()), argv.data());
  return exit_code == 0;
}

} // namespace

int main() {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const fs::path root = fs::temp_directory_path() / ("labops-bundle-zip-on-demand-" + std::to_string(now_ms));
  const fs::path scenario_path = root / "scenario.json";
  const fs::path out_zip = root / "out-with-zip";
  const fs::path out_no_zip = root / "out-no-zip";

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  {
    std::ofstream scenario_file(scenario_path, std::ios::binary);
    scenario_file << "{\n"
                  << "  \"name\": \"zip-on-demand\",\n"
                  << "  \"duration_ms\": 500,\n"
                  << "  \"fps\": 30,\n"
                  << "  \"jitter_us\": 0,\n"
                  << "  \"seed\": 123,\n"
                  << "  \"frame_size_bytes\": 2048,\n"
                  << "  \"drop_every_n\": 0,\n"
                  << "  \"drop_percent\": 0,\n"
                  << "  \"burst_drop\": 0,\n"
                  << "  \"reorder\": 0\n"
                  << "}\n";
  }

  if (!RunScenario(scenario_path, out_zip, true)) {
    Fail("labops run with --zip failed");
  }
  const fs::path bundle_with_zip = ResolveSingleBundleDir(out_zip);
  const fs::path zip_path = bundle_with_zip.string() + ".zip";
  if (!fs::exists(zip_path)) {
    Fail("expected bundle zip was not produced when --zip was requested");
  }

  std::ifstream zip_in(zip_path, std::ios::binary);
  if (!zip_in) {
    Fail("failed to open produced bundle zip");
  }
  char sig[4] = {0, 0, 0, 0};
  zip_in.read(sig, 4);
  if (zip_in.gcount() != 4 || !(sig[0] == 'P' && sig[1] == 'K' && sig[2] == 3 && sig[3] == 4)) {
    Fail("produced bundle zip has invalid signature");
  }

  if (!RunScenario(scenario_path, out_no_zip, false)) {
    Fail("labops run without --zip failed");
  }
  const fs::path bundle_without_zip = ResolveSingleBundleDir(out_no_zip);
  const fs::path unexpected_zip_path = bundle_without_zip.string() + ".zip";
  if (fs::exists(unexpected_zip_path)) {
    Fail("bundle zip should not be produced when --zip is not requested");
  }

  fs::remove_all(root, ec);
  std::cout << "bundle_zip_on_demand_smoke: ok\n";
  return 0;
}
