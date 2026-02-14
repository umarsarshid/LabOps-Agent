#include "artifacts/bundle_zip_writer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace labops::artifacts {

namespace {

constexpr std::uint32_t kLocalFileHeaderSignature = 0x04034b50U;
constexpr std::uint32_t kCentralDirectoryHeaderSignature = 0x02014b50U;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50U;
constexpr std::uint16_t kZipVersion = 20; // 2.0
constexpr std::uint16_t kCompressionMethodStore = 0;

constexpr std::uint32_t kCrc32Init = 0xFFFFFFFFU;
constexpr std::uint32_t kCrc32FinalXor = 0xFFFFFFFFU;

struct FileEntry {
  fs::path path;
  std::string zip_path;
  std::uint32_t crc32 = 0;
  std::uint32_t size_bytes = 0;
  std::uint32_t local_header_offset = 0;
};

void WriteU16(std::ofstream& out_file, std::uint16_t value) {
  const std::array<char, 2> bytes = {
      static_cast<char>(value & 0xFFU),
      static_cast<char>((value >> 8) & 0xFFU),
  };
  out_file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void WriteU32(std::ofstream& out_file, std::uint32_t value) {
  const std::array<char, 4> bytes = {
      static_cast<char>(value & 0xFFU),
      static_cast<char>((value >> 8) & 0xFFU),
      static_cast<char>((value >> 16) & 0xFFU),
      static_cast<char>((value >> 24) & 0xFFU),
  };
  out_file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

const std::array<std::uint32_t, 256>& Crc32Table() {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> generated{};
    for (std::uint32_t i = 0; i < 256U; ++i) {
      std::uint32_t c = i;
      for (int bit = 0; bit < 8; ++bit) {
        if ((c & 1U) != 0U) {
          c = 0xEDB88320U ^ (c >> 1);
        } else {
          c >>= 1;
        }
      }
      generated[i] = c;
    }
    return generated;
  }();
  return table;
}

std::uint32_t Crc32Update(std::uint32_t crc, const char* data, std::size_t size) {
  const auto& table = Crc32Table();
  std::uint32_t c = crc;
  for (std::size_t i = 0; i < size; ++i) {
    const std::uint8_t byte = static_cast<std::uint8_t>(data[i]);
    c = table[(c ^ byte) & 0xFFU] ^ (c >> 8);
  }
  return c;
}

bool ComputeFileCrcAndSize(const fs::path& path, std::uint32_t& crc32, std::uint32_t& size_bytes,
                           std::string& error) {
  std::ifstream in_file(path, std::ios::binary);
  if (!in_file) {
    error = "failed to open file for zip crc: " + path.string();
    return false;
  }

  std::uint32_t crc = kCrc32Init;
  std::uint64_t total_size = 0;
  std::array<char, 8192> buffer{};
  while (in_file.good()) {
    in_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = in_file.gcount();
    if (read_count <= 0) {
      continue;
    }
    crc = Crc32Update(crc, buffer.data(), static_cast<std::size_t>(read_count));
    total_size += static_cast<std::uint64_t>(read_count);
  }

  if (!in_file.eof()) {
    error = "failed while reading file for zip crc: " + path.string();
    return false;
  }
  if (total_size > 0xFFFFFFFFULL) {
    error = "file too large for zip32 support: " + path.string();
    return false;
  }

  crc32 = crc ^ kCrc32FinalXor;
  size_bytes = static_cast<std::uint32_t>(total_size);
  return true;
}

bool CopyFileToStream(const fs::path& path, std::ofstream& out_file, std::string& error) {
  std::ifstream in_file(path, std::ios::binary);
  if (!in_file) {
    error = "failed to open file for zip payload: " + path.string();
    return false;
  }

  std::array<char, 8192> buffer{};
  while (in_file.good()) {
    in_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = in_file.gcount();
    if (read_count <= 0) {
      continue;
    }
    out_file.write(buffer.data(), read_count);
    if (!out_file) {
      error = "failed while writing zip payload for file: " + path.string();
      return false;
    }
  }

  if (!in_file.eof()) {
    error = "failed while reading zip payload source: " + path.string();
    return false;
  }

  return true;
}

bool CollectBundleFiles(const fs::path& bundle_dir, std::vector<fs::path>& file_paths,
                        std::string& error) {
  std::error_code ec;
  if (!fs::exists(bundle_dir, ec) || ec) {
    error = "bundle directory not found: " + bundle_dir.string();
    return false;
  }
  if (!fs::is_directory(bundle_dir, ec) || ec) {
    error = "bundle path must be a directory: " + bundle_dir.string();
    return false;
  }

  file_paths.clear();
  for (const auto& entry : fs::recursive_directory_iterator(bundle_dir, ec)) {
    if (ec) {
      error = "failed while enumerating bundle directory: " + bundle_dir.string();
      return false;
    }
    if (entry.is_regular_file()) {
      file_paths.push_back(entry.path());
    }
  }

  std::sort(file_paths.begin(), file_paths.end());
  if (file_paths.empty()) {
    error = "bundle directory contains no files: " + bundle_dir.string();
    return false;
  }

  return true;
}

bool ValidateZipPathLength(const std::string& zip_path, std::string& error) {
  if (zip_path.empty()) {
    error = "zip entry path cannot be empty";
    return false;
  }
  if (zip_path.size() > 0xFFFFU) {
    error = "zip entry path too long: " + zip_path;
    return false;
  }
  return true;
}

} // namespace

bool WriteBundleZip(const fs::path& bundle_dir, fs::path& written_path, std::string& error) {
  if (bundle_dir.empty()) {
    error = "bundle directory cannot be empty";
    return false;
  }

  std::vector<fs::path> file_paths;
  if (!CollectBundleFiles(bundle_dir, file_paths, error)) {
    return false;
  }

  const fs::path parent_dir = bundle_dir.parent_path();
  const std::string bundle_name = bundle_dir.filename().string();
  if (bundle_name.empty()) {
    error = "bundle directory must have a valid name";
    return false;
  }

  std::vector<FileEntry> files;
  files.reserve(file_paths.size());
  for (const auto& path : file_paths) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, bundle_dir, ec);
    if (ec || relative.empty()) {
      error = "failed to compute path relative to bundle: " + path.string();
      return false;
    }
    const std::string relative_zip = relative.generic_string();
    if (relative_zip.rfind("..", 0) == 0U) {
      error = "file is outside bundle directory: " + path.string();
      return false;
    }

    FileEntry entry;
    entry.path = path;
    entry.zip_path = bundle_name + "/" + relative_zip;
    if (!ValidateZipPathLength(entry.zip_path, error)) {
      return false;
    }
    if (!ComputeFileCrcAndSize(entry.path, entry.crc32, entry.size_bytes, error)) {
      return false;
    }
    files.push_back(std::move(entry));
  }

  if (files.size() > 0xFFFFU) {
    error = "too many files for zip32 support";
    return false;
  }

  written_path = bundle_dir;
  written_path += ".zip";
  std::ofstream out_file(written_path, std::ios::binary | std::ios::trunc);
  if (!out_file) {
    error = "failed to open bundle zip output: " + written_path.string();
    return false;
  }

  // Local file headers + file data.
  for (auto& entry : files) {
    const std::streamoff offset = out_file.tellp();
    if (offset < 0 || static_cast<std::uint64_t>(offset) > 0xFFFFFFFFULL) {
      error = "zip offset overflow while writing local file headers";
      return false;
    }
    entry.local_header_offset = static_cast<std::uint32_t>(offset);

    WriteU32(out_file, kLocalFileHeaderSignature);
    WriteU16(out_file, kZipVersion);
    WriteU16(out_file, 0); // general purpose bit flag
    WriteU16(out_file, kCompressionMethodStore);
    WriteU16(out_file, 0); // last mod file time
    WriteU16(out_file, 0); // last mod file date
    WriteU32(out_file, entry.crc32);
    WriteU32(out_file, entry.size_bytes); // compressed size (store)
    WriteU32(out_file, entry.size_bytes); // uncompressed size
    WriteU16(out_file, static_cast<std::uint16_t>(entry.zip_path.size()));
    WriteU16(out_file, 0); // extra field length
    out_file.write(entry.zip_path.data(), static_cast<std::streamsize>(entry.zip_path.size()));
    if (!out_file) {
      error = "failed while writing zip local file header";
      return false;
    }

    if (!CopyFileToStream(entry.path, out_file, error)) {
      return false;
    }
  }

  const std::streamoff central_dir_offset_stream = out_file.tellp();
  if (central_dir_offset_stream < 0 ||
      static_cast<std::uint64_t>(central_dir_offset_stream) > 0xFFFFFFFFULL) {
    error = "zip central directory offset overflow";
    return false;
  }
  const std::uint32_t central_dir_offset = static_cast<std::uint32_t>(central_dir_offset_stream);

  // Central directory entries.
  for (const auto& entry : files) {
    WriteU32(out_file, kCentralDirectoryHeaderSignature);
    WriteU16(out_file, kZipVersion); // version made by
    WriteU16(out_file, kZipVersion); // version needed to extract
    WriteU16(out_file, 0);           // general purpose bit flag
    WriteU16(out_file, kCompressionMethodStore);
    WriteU16(out_file, 0); // last mod file time
    WriteU16(out_file, 0); // last mod file date
    WriteU32(out_file, entry.crc32);
    WriteU32(out_file, entry.size_bytes); // compressed size
    WriteU32(out_file, entry.size_bytes); // uncompressed size
    WriteU16(out_file, static_cast<std::uint16_t>(entry.zip_path.size()));
    WriteU16(out_file, 0); // extra field length
    WriteU16(out_file, 0); // file comment length
    WriteU16(out_file, 0); // disk number start
    WriteU16(out_file, 0); // internal file attributes
    WriteU32(out_file, 0); // external file attributes
    WriteU32(out_file, entry.local_header_offset);
    out_file.write(entry.zip_path.data(), static_cast<std::streamsize>(entry.zip_path.size()));
    if (!out_file) {
      error = "failed while writing zip central directory";
      return false;
    }
  }

  const std::streamoff central_dir_end_stream = out_file.tellp();
  if (central_dir_end_stream < 0 ||
      static_cast<std::uint64_t>(central_dir_end_stream) > 0xFFFFFFFFULL) {
    error = "zip central directory size overflow";
    return false;
  }
  if (central_dir_end_stream < central_dir_offset_stream) {
    error = "zip central directory offsets are invalid";
    return false;
  }
  const std::uint32_t central_dir_size =
      static_cast<std::uint32_t>(central_dir_end_stream - central_dir_offset_stream);

  // End of central directory record.
  WriteU32(out_file, kEndOfCentralDirectorySignature);
  WriteU16(out_file, 0); // number of this disk
  WriteU16(out_file, 0); // number of the disk with the start of the central directory
  WriteU16(out_file, static_cast<std::uint16_t>(files.size()));
  WriteU16(out_file, static_cast<std::uint16_t>(files.size()));
  WriteU32(out_file, central_dir_size);
  WriteU32(out_file, central_dir_offset);
  WriteU16(out_file, 0); // zip file comment length

  if (!out_file) {
    error = "failed while finalizing bundle zip file";
    return false;
  }

  return true;
}

} // namespace labops::artifacts
