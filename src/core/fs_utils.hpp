#ifndef LABOPS_CORE_FS_UTILS_HPP_
#define LABOPS_CORE_FS_UTILS_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace labops::core {

namespace detail {

inline std::filesystem::path BuildAtomicTempPath(const std::filesystem::path& output_path) {
  static std::atomic<std::uint64_t> counter{0};
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::uint64_t suffix = counter.fetch_add(1U, std::memory_order_relaxed);
  return output_path.string() + ".tmp." + std::to_string(tick) + "." + std::to_string(suffix);
}

} // namespace detail

inline bool EnsureParentDirectory(const std::filesystem::path& output_path, std::string& error) {
  if (output_path.empty()) {
    error = "output path cannot be empty";
    return false;
  }

  const std::filesystem::path parent_dir = output_path.parent_path();
  if (parent_dir.empty()) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(parent_dir, ec);
  if (ec) {
    error = "failed to create output directory '" + parent_dir.string() + "': " + ec.message();
    return false;
  }

  return true;
}

// Best-effort atomic text file write:
// 1) write full content to a temporary sibling file
// 2) rename temp file into final destination
//
// On platforms/filesystems where rename-overwrite is restricted, we attempt a
// remove+rename fallback while still ensuring partially written output files are
// not published.
inline bool WriteTextFileAtomic(const std::filesystem::path& output_path, std::string_view text,
                                std::string& error) {
  if (!EnsureParentDirectory(output_path, error)) {
    return false;
  }

  const std::filesystem::path temp_path = detail::BuildAtomicTempPath(output_path);
  {
    std::ofstream out_file(temp_path, std::ios::binary | std::ios::trunc);
    if (!out_file) {
      error = "failed to open temp output file '" + temp_path.string() + "'";
      return false;
    }

    out_file << text;
    if (!out_file) {
      error = "failed while writing temp output file '" + temp_path.string() + "'";
      return false;
    }
  }

  std::error_code rename_ec;
  std::filesystem::rename(temp_path, output_path, rename_ec);
  if (!rename_ec) {
    return true;
  }

  std::error_code remove_ec;
  (void)std::filesystem::remove(output_path, remove_ec);
  rename_ec.clear();
  std::filesystem::rename(temp_path, output_path, rename_ec);
  if (!rename_ec) {
    return true;
  }

  std::error_code cleanup_ec;
  (void)std::filesystem::remove(temp_path, cleanup_ec);
  error = "failed to publish output file '" + output_path.string() + "': " + rename_ec.message();
  return false;
}

} // namespace labops::core

#endif // LABOPS_CORE_FS_UTILS_HPP_
