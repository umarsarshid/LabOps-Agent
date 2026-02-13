#include "artifacts/scenario_writer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

} // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "labops-scenario-writer-smoke";
  const fs::path source = root / "source.json";
  const fs::path out_dir = root / "bundle";

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  {
    std::ofstream source_file(source, std::ios::binary);
    source_file << "{\n"
                << "  \"schema_version\": \"1.0\",\n"
                << "  \"scenario_id\": \"smoke\"\n"
                << "}\n";
  }

  fs::path written_path;
  std::string error;
  if (!labops::artifacts::WriteScenarioJson(source, out_dir, written_path, error)) {
    Fail("WriteScenarioJson failed: " + error);
  }

  if (written_path != out_dir / "scenario.json") {
    Fail("unexpected written scenario path");
  }
  if (!fs::exists(written_path)) {
    Fail("scenario.json was not produced");
  }

  std::ifstream source_in(source, std::ios::binary);
  std::ifstream written_in(written_path, std::ios::binary);
  if (!source_in || !written_in) {
    Fail("failed to open source or written scenario file");
  }

  const std::string source_text((std::istreambuf_iterator<char>(source_in)),
                                std::istreambuf_iterator<char>());
  const std::string written_text((std::istreambuf_iterator<char>(written_in)),
                                 std::istreambuf_iterator<char>());
  if (source_text != written_text) {
    Fail("written scenario.json content mismatch");
  }

  fs::remove_all(root, ec);
  std::cout << "scenario_writer_smoke: ok\n";
  return 0;
}
