#ifndef LABOPS_ARTIFACTS_BUNDLE_REGISTRY_HPP_
#define LABOPS_ARTIFACTS_BUNDLE_REGISTRY_HPP_

#include <filesystem>
#include <vector>

namespace labops::artifacts {

// Small helper for assembling bundle-manifest input paths in one place.
//
// Contract:
// - required artifacts are always included in BuildManifestInput output
// - optional artifacts are included only when non-empty and present on disk
class BundleArtifactRegistry {
public:
  void RegisterRequired(const std::filesystem::path& artifact_path);
  void RegisterOptional(const std::filesystem::path& artifact_path);
  void RegisterMany(const std::vector<std::filesystem::path>& artifact_paths);

  // Produces the final ordered path list consumed by WriteBundleManifestJson.
  std::vector<std::filesystem::path> BuildManifestInput() const;

private:
  std::vector<std::filesystem::path> required_artifacts_;
  std::vector<std::filesystem::path> optional_artifacts_;
};

} // namespace labops::artifacts

#endif // LABOPS_ARTIFACTS_BUNDLE_REGISTRY_HPP_
