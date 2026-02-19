#include "artifacts/bundle_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertPathEquals(const fs::path& actual, const fs::path& expected, std::string_view label) {
  if (actual != expected) {
    std::cerr << "path mismatch for " << label << '\n';
    std::cerr << "expected: " << expected.string() << '\n';
    std::cerr << "actual: " << actual.string() << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "labops-bundle-registry-smoke";
  const fs::path required_existing = root / "required-existing.txt";
  const fs::path required_missing = root / "required-missing.txt";
  const fs::path many_existing = root / "many-existing.txt";
  const fs::path optional_existing = root / "optional-existing.txt";
  const fs::path optional_missing = root / "optional-missing.txt";

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  if (ec) {
    Fail("failed to create temp root");
  }

  {
    std::ofstream out(required_existing, std::ios::binary);
    out << "required\n";
    if (!out) {
      Fail("failed to write required-existing fixture");
    }
  }
  {
    std::ofstream out(many_existing, std::ios::binary);
    out << "many\n";
    if (!out) {
      Fail("failed to write many-existing fixture");
    }
  }
  {
    std::ofstream out(optional_existing, std::ios::binary);
    out << "optional\n";
    if (!out) {
      Fail("failed to write optional-existing fixture");
    }
  }

  labops::artifacts::BundleArtifactRegistry registry;
  registry.RegisterRequired(required_existing);
  registry.RegisterRequired(required_missing);
  registry.RegisterMany({many_existing});
  registry.RegisterOptional(optional_missing);
  registry.RegisterOptional(optional_existing);

  const std::vector<fs::path> manifest_input = registry.BuildManifestInput();
  if (manifest_input.size() != 4U) {
    Fail("unexpected manifest input size");
  }

  // Keep ordering stable: required registrations first, optional existing
  // paths appended in registration order.
  AssertPathEquals(manifest_input[0], required_existing, "required_existing");
  AssertPathEquals(manifest_input[1], required_missing, "required_missing");
  AssertPathEquals(manifest_input[2], many_existing, "many_existing");
  AssertPathEquals(manifest_input[3], optional_existing, "optional_existing");

  fs::remove_all(root, ec);
  std::cout << "bundle_registry_smoke: ok\n";
  return 0;
}
