#include "artifacts/bundle_zip_writer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

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

} // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "labops-bundle-zip-writer-smoke";
  const fs::path bundle_dir = root / "run-zip-smoke";

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(bundle_dir, ec);
  if (ec) {
    Fail("failed to create temp bundle dir");
  }

  const std::vector<std::pair<std::string, std::string>> files = {
      {"scenario.json", "{\"scenario_id\":\"zip_smoke\"}\n"},
      {"run.json", "{\"run_id\":\"run-zip-smoke\"}\n"},
      {"events.jsonl", "{\"type\":\"STREAM_STARTED\"}\n"},
      {"metrics.csv", "metric,frames\\navg_fps,10\\n"},
      {"metrics.json", "{\"avg_fps\":10.0}\n"},
      {"bundle_manifest.json", "{\"schema_version\":\"1.0\"}\n"},
  };

  for (const auto& [name, content] : files) {
    const fs::path path = bundle_dir / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    if (!out) {
      Fail("failed to write test bundle artifact: " + name);
    }
  }

  fs::path written_path;
  std::string error;
  if (!labops::artifacts::WriteBundleZip(bundle_dir, written_path, error)) {
    Fail("WriteBundleZip failed: " + error);
  }

  const fs::path expected_zip = root / "run-zip-smoke.zip";
  if (written_path != expected_zip) {
    Fail("unexpected bundle zip output path");
  }
  if (!fs::exists(written_path)) {
    Fail("bundle zip file was not produced");
  }

  std::ifstream zip_in(written_path, std::ios::binary);
  if (!zip_in) {
    Fail("failed to open bundle zip file");
  }

  char signature[4] = {0, 0, 0, 0};
  zip_in.read(signature, 4);
  if (zip_in.gcount() != 4) {
    Fail("bundle zip too short");
  }
  if (!(signature[0] == 'P' && signature[1] == 'K' && signature[2] == 3 && signature[3] == 4)) {
    Fail("bundle zip signature mismatch");
  }

  zip_in.clear();
  zip_in.seekg(0, std::ios::beg);
  const std::string zip_text((std::istreambuf_iterator<char>(zip_in)),
                             std::istreambuf_iterator<char>());
  AssertContains(zip_text, "run-zip-smoke/scenario.json");
  AssertContains(zip_text, "run-zip-smoke/run.json");
  AssertContains(zip_text, "run-zip-smoke/events.jsonl");
  AssertContains(zip_text, "run-zip-smoke/metrics.csv");
  AssertContains(zip_text, "run-zip-smoke/metrics.json");
  AssertContains(zip_text, "run-zip-smoke/bundle_manifest.json");

  fs::remove_all(root, ec);
  std::cout << "bundle_zip_writer_smoke: ok\n";
  return 0;
}
