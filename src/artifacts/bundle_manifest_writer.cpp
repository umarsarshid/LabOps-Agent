#include "artifacts/bundle_manifest_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

struct ManifestEntry {
  std::string relative_path;
  std::uintmax_t size_bytes = 0;
  std::string hash_hex;
};

std::string EscapeJson(std::string_view input) {
  std::ostringstream out;
  for (const char ch : input) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default: {
      const auto as_unsigned = static_cast<unsigned char>(ch);
      if (as_unsigned < 0x20U) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(as_unsigned) << std::dec << std::setfill(' ');
      } else {
        out << ch;
      }
      break;
    }
    }
  }
  return out.str();
}

bool ComputeFileFnv1a64(const fs::path& file_path, std::string& hash_hex, std::string& error) {
  std::ifstream in_file(file_path, std::ios::binary);
  if (!in_file) {
    error = "failed to open file for hashing: " + file_path.string();
    return false;
  }

  std::uint64_t hash = kFnv1a64OffsetBasis;
  char buffer[4096];
  while (in_file.good()) {
    in_file.read(buffer, sizeof(buffer));
    const std::streamsize read_count = in_file.gcount();
    if (read_count <= 0) {
      continue;
    }
    for (std::streamsize i = 0; i < read_count; ++i) {
      const std::uint8_t byte = static_cast<std::uint8_t>(buffer[i]);
      hash ^= static_cast<std::uint64_t>(byte);
      hash *= kFnv1a64Prime;
    }
  }

  if (!in_file.eof()) {
    error = "failed while reading file for hashing: " + file_path.string();
    return false;
  }

  std::ostringstream out;
  out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << hash;
  hash_hex = out.str();
  return true;
}

} // namespace

bool WriteBundleManifestJson(const fs::path& bundle_dir,
                             const std::vector<fs::path>& artifact_paths, fs::path& written_path,
                             std::string& error) {
  if (bundle_dir.empty()) {
    error = "bundle directory cannot be empty";
    return false;
  }
  if (artifact_paths.empty()) {
    error = "artifact path list cannot be empty";
    return false;
  }

  std::error_code ec;
  fs::create_directories(bundle_dir, ec);
  if (ec) {
    error = "failed to create bundle directory '" + bundle_dir.string() + "': " + ec.message();
    return false;
  }

  std::vector<ManifestEntry> entries;
  entries.reserve(artifact_paths.size());

  for (const auto& artifact_path : artifact_paths) {
    if (artifact_path.empty()) {
      error = "artifact path cannot be empty";
      return false;
    }
    if (!fs::exists(artifact_path, ec) || ec) {
      error = "artifact file not found: " + artifact_path.string();
      return false;
    }
    if (!fs::is_regular_file(artifact_path, ec) || ec) {
      error = "artifact path must be a regular file: " + artifact_path.string();
      return false;
    }

    const fs::path relative_path = fs::relative(artifact_path, bundle_dir, ec);
    if (ec || relative_path.empty()) {
      error = "failed to compute artifact path relative to bundle: " + artifact_path.string();
      return false;
    }
    if (relative_path.string().rfind("..", 0) == 0U) {
      error = "artifact is outside bundle directory: " + artifact_path.string();
      return false;
    }

    ManifestEntry entry;
    entry.relative_path = relative_path.generic_string();
    entry.size_bytes = fs::file_size(artifact_path, ec);
    if (ec) {
      error = "failed to read file size for artifact: " + artifact_path.string();
      return false;
    }
    if (!ComputeFileFnv1a64(artifact_path, entry.hash_hex, error)) {
      return false;
    }
    entries.push_back(std::move(entry));
  }

  std::sort(entries.begin(), entries.end(), [](const ManifestEntry& lhs, const ManifestEntry& rhs) {
    return lhs.relative_path < rhs.relative_path;
  });

  written_path = bundle_dir / "bundle_manifest.json";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open output file '" + written_path.string() + "' for writing";
    return false;
  }

  out_file << "{\n"
           << "  \"schema_version\":\"1.0\",\n"
           << "  \"hash_algorithm\":\"fnv1a_64\",\n"
           << "  \"files\":[";

  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (i != 0U) {
      out_file << ",";
    }
    out_file << "\n    {\"path\":\"" << EscapeJson(entry.relative_path) << "\","
             << "\"size_bytes\":" << entry.size_bytes << ","
             << "\"hash\":\"" << entry.hash_hex << "\"}";
  }

  out_file << "\n  ]\n"
           << "}\n";

  if (!out_file) {
    error = "failed while writing output file '" + written_path.string() + "'";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
