#include "backends/real_sdk/real_backend_factory.hpp"

#include "backends/real_sdk/real_backend.hpp"
#include "backends/real_sdk/sdk_context.hpp"
#include "backends/sdk_stub/real_camera_backend_stub.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace labops::backends::real_sdk {

namespace {

struct SdkDeviceDescriptor {
  std::string model;
  std::string serial;
  std::string user_id;
  std::string transport;
  std::string ip_address;
  std::string mac_address;
};

std::string Trim(std::string_view input) {
  std::size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
    ++begin;
  }

  std::size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
    --end;
  }
  return std::string(input.substr(begin, end - begin));
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::vector<std::string> SplitCsvSimple(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  current.reserve(line.size());
  for (char c : line) {
    if (c == ',') {
      fields.push_back(Trim(current));
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  fields.push_back(Trim(current));
  return fields;
}

bool ParseDescriptorCsvLine(const std::string& line, const std::size_t line_number,
                            SdkDeviceDescriptor& descriptor, std::string& error) {
  const std::vector<std::string> fields = SplitCsvSimple(line);
  if (fields.size() < 4U) {
    error = "device fixture parse error at line " + std::to_string(line_number) +
            ": expected at least 4 CSV fields (model,serial,user_id,transport)";
    return false;
  }

  descriptor.model = fields[0];
  descriptor.serial = fields[1];
  descriptor.user_id = fields[2];
  descriptor.transport = fields[3];
  descriptor.ip_address = fields.size() >= 5U ? fields[4] : "";
  descriptor.mac_address = fields.size() >= 6U ? fields[5] : "";
  return true;
}

bool LooksLikeHeader(const SdkDeviceDescriptor& descriptor) {
  return ToLower(descriptor.model) == "model" && ToLower(descriptor.serial) == "serial" &&
         ToLower(descriptor.transport) == "transport";
}

bool LoadDescriptorsFromFixture(const fs::path& fixture_path,
                                std::vector<SdkDeviceDescriptor>& descriptors, std::string& error) {
  std::ifstream input(fixture_path, std::ios::binary);
  if (!input) {
    error = "unable to open LABOPS_REAL_DEVICE_FIXTURE file: " + fixture_path.string();
    return false;
  }

  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed.rfind("#", 0) == 0U) {
      continue;
    }

    SdkDeviceDescriptor descriptor;
    if (!ParseDescriptorCsvLine(trimmed, line_number, descriptor, error)) {
      return false;
    }
    if (LooksLikeHeader(descriptor)) {
      continue;
    }
    descriptors.push_back(std::move(descriptor));
  }

  return true;
}

bool CollectSdkDeviceDescriptors(std::vector<SdkDeviceDescriptor>& descriptors,
                                 std::string& error) {
  descriptors.clear();
  error.clear();

  // Placeholder discovery source used in OSS builds.
  // Future vendor SDK integration should replace this with direct SDK
  // enumeration calls and keep the mapping contract below stable.
  const char* fixture_path = std::getenv("LABOPS_REAL_DEVICE_FIXTURE");
  if (fixture_path == nullptr || *fixture_path == '\0') {
    return true;
  }

  return LoadDescriptorsFromFixture(fs::path(fixture_path), descriptors, error);
}

std::optional<std::string> NormalizeOptionalField(const std::string& value) {
  const std::string trimmed = Trim(value);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  return trimmed;
}

std::string NormalizeRequiredField(const std::string& value, std::string_view fallback) {
  const std::string trimmed = Trim(value);
  if (!trimmed.empty()) {
    return trimmed;
  }
  return std::string(fallback);
}

std::string NormalizeTransport(const std::string& value) {
  const std::string normalized = ToLower(Trim(value));
  if (normalized.empty()) {
    return "unknown";
  }
  if (normalized == "gige" || normalized == "gig e" || normalized == "gigabit_ethernet") {
    return "gige";
  }
  if (normalized == "usb" || normalized == "usb3" || normalized == "usb3vision") {
    return "usb";
  }
  if (normalized == "cxp" || normalized == "coaxpress") {
    return "cxp";
  }
  return normalized;
}

std::optional<std::string> NormalizeMac(const std::string& value) {
  std::optional<std::string> normalized = NormalizeOptionalField(value);
  if (!normalized.has_value()) {
    return std::nullopt;
  }
  std::replace(normalized->begin(), normalized->end(), '-', ':');
  std::transform(normalized->begin(), normalized->end(), normalized->begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return normalized;
}

DeviceInfo MapDescriptorToDeviceInfo(const SdkDeviceDescriptor& descriptor) {
  DeviceInfo info;
  info.model = NormalizeRequiredField(descriptor.model, "unknown_model");
  info.serial = NormalizeRequiredField(descriptor.serial, "unknown_serial");
  info.user_id = NormalizeOptionalField(descriptor.user_id).value_or("");
  info.transport = NormalizeTransport(descriptor.transport);
  info.ip_address = NormalizeOptionalField(descriptor.ip_address);
  info.mac_address = NormalizeMac(descriptor.mac_address);
  return info;
}

} // namespace

bool IsRealBackendEnabledAtBuild() {
  return sdk_stub::IsRealBackendEnabledAtBuild();
}

bool WasRealBackendRequestedAtBuild() {
  return sdk_stub::WasRealBackendRequestedAtBuild();
}

std::string_view RealBackendAvailabilityStatusText() {
  return sdk_stub::RealBackendAvailabilityStatusText();
}

std::unique_ptr<ICameraBackend> CreateRealBackend() {
  if (IsRealBackendEnabledAtBuild()) {
    return std::make_unique<RealBackend>();
  }
  return std::make_unique<sdk_stub::RealCameraBackendStub>();
}

bool EnumerateConnectedDevices(std::vector<DeviceInfo>& devices, std::string& error) {
  devices.clear();
  error.clear();

  if (!IsRealBackendEnabledAtBuild()) {
    error = std::string("real backend ") + std::string(RealBackendAvailabilityStatusText());
    return false;
  }

  SdkContext sdk_context;
  if (!sdk_context.Acquire(error)) {
    return false;
  }

  std::vector<SdkDeviceDescriptor> descriptors;
  if (!CollectSdkDeviceDescriptors(descriptors, error)) {
    sdk_context.Release();
    return false;
  }
  sdk_context.Release();

  devices.reserve(descriptors.size());
  for (const SdkDeviceDescriptor& descriptor : descriptors) {
    devices.push_back(MapDescriptorToDeviceInfo(descriptor));
  }
  return true;
}

} // namespace labops::backends::real_sdk
