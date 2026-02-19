#include "artifacts/bundle_registry.hpp"

#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

void BundleArtifactRegistry::RegisterRequired(const fs::path& artifact_path) {
  required_artifacts_.push_back(artifact_path);
}

void BundleArtifactRegistry::RegisterOptional(const fs::path& artifact_path) {
  optional_artifacts_.push_back(artifact_path);
}

void BundleArtifactRegistry::RegisterMany(const std::vector<fs::path>& artifact_paths) {
  required_artifacts_.insert(required_artifacts_.end(), artifact_paths.begin(),
                             artifact_paths.end());
}

std::vector<fs::path> BundleArtifactRegistry::BuildManifestInput() const {
  std::vector<fs::path> artifact_paths = required_artifacts_;

  for (const fs::path& path : optional_artifacts_) {
    if (path.empty()) {
      continue;
    }
    std::error_code ec;
    if (fs::exists(path, ec) && !ec) {
      artifact_paths.push_back(path);
    }
  }

  return artifact_paths;
}

} // namespace labops::artifacts
