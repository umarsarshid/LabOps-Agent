#include "artifacts/bundle_manifest_writer.hpp"

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

std::size_t CountOccurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t match = text.find(needle, pos);
    if (match == std::string_view::npos) {
      break;
    }
    ++count;
    pos = match + needle.size();
  }
  return count;
}

} // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "labops-bundle-manifest-writer-smoke";
  const fs::path bundle_dir = root / "run-123";

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(bundle_dir, ec);
  if (ec) {
    Fail("failed to create temp bundle dir");
  }

  const std::vector<std::pair<std::string, std::string>> files = {
      {"scenario.json", "{\"scenario_id\":\"smoke\"}\n"},
      {"run.json", "{\"run_id\":\"run-123\"}\n"},
      {"events.jsonl", "{\"type\":\"STREAM_STARTED\"}\n"},
      {"metrics.csv", "metric,frames\\navg_fps,10\\n"},
      {"metrics.json", "{\"avg_fps\":10.0}\n"},
  };

  std::vector<fs::path> artifact_paths;
  artifact_paths.reserve(files.size());
  for (const auto& [name, content] : files) {
    const fs::path file_path = bundle_dir / name;
    std::ofstream out(file_path, std::ios::binary);
    out << content;
    if (!out) {
      Fail("failed to write test artifact: " + name);
    }
    artifact_paths.push_back(file_path);
  }

  fs::path written_path;
  std::string error;
  if (!labops::artifacts::WriteBundleManifestJson(bundle_dir, artifact_paths, written_path, error)) {
    Fail("WriteBundleManifestJson failed: " + error);
  }

  if (written_path != bundle_dir / "bundle_manifest.json") {
    Fail("unexpected bundle manifest path");
  }
  if (!fs::exists(written_path)) {
    Fail("bundle manifest file was not produced");
  }

  std::ifstream in(written_path, std::ios::binary);
  if (!in) {
    Fail("failed to open bundle manifest file");
  }
  const std::string manifest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  AssertContains(manifest, "\"schema_version\":\"1.0\"");
  AssertContains(manifest, "\"hash_algorithm\":\"fnv1a_64\"");

  for (const auto& [name, _] : files) {
    AssertContains(manifest, "\"path\":\"" + name + "\"");
  }

  // Every listed file should include both hash and size fields.
  if (CountOccurrences(manifest, "\"hash\":\"") != files.size()) {
    Fail("manifest hash entry count mismatch");
  }
  if (CountOccurrences(manifest, "\"size_bytes\":") != files.size()) {
    Fail("manifest size entry count mismatch");
  }

  fs::remove_all(root, ec);
  std::cout << "bundle_manifest_writer_smoke: ok\n";
  return 0;
}
