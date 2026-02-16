#include "backends/real_sdk/real_backend_factory.hpp"

#include "backends/real_sdk/real_backend.hpp"
#include "backends/real_sdk/sdk_context.hpp"
#include "backends/sdk_stub/real_camera_backend_stub.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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
  std::string firmware_version;
  std::string sdk_version;
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

std::vector<std::string> SplitSelectorClauses(std::string_view raw) {
  std::vector<std::string> clauses;
  std::string current;
  current.reserve(raw.size());
  for (const char c : raw) {
    if (c == ',') {
      clauses.push_back(Trim(current));
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  clauses.push_back(Trim(current));
  return clauses;
}

bool ParseNonNegativeIndex(std::string_view raw, std::size_t& parsed_index) {
  if (raw.empty()) {
    return false;
  }
  std::size_t parsed = 0;
  const char* begin = raw.data();
  const char* end = begin + raw.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return false;
  }
  parsed_index = parsed;
  return true;
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
  descriptor.firmware_version = fields.size() >= 7U ? fields[6] : "";
  descriptor.sdk_version = fields.size() >= 8U ? fields[7] : "";
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
  info.firmware_version = NormalizeOptionalField(descriptor.firmware_version);
  info.sdk_version = NormalizeOptionalField(descriptor.sdk_version);
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

bool ParseDeviceSelector(std::string_view selector_text, DeviceSelector& selector,
                         std::string& error) {
  selector = DeviceSelector{};
  error.clear();

  const std::string trimmed = Trim(selector_text);
  if (trimmed.empty()) {
    error = "selector cannot be empty";
    return false;
  }

  const std::vector<std::string> clauses = SplitSelectorClauses(trimmed);
  for (const std::string& clause : clauses) {
    if (clause.empty()) {
      error = "selector contains an empty clause";
      return false;
    }

    const std::size_t colon = clause.find(':');
    if (colon == std::string::npos) {
      error = "selector clause '" + clause + "' must use key:value format";
      return false;
    }

    const std::string key = ToLower(Trim(clause.substr(0, colon)));
    const std::string value = Trim(clause.substr(colon + 1));
    if (value.empty()) {
      error = "selector clause '" + clause + "' must provide a non-empty value (missing a value)";
      return false;
    }

    if (key == "serial") {
      if (selector.serial.has_value()) {
        error = "selector contains duplicate serial key";
        return false;
      }
      selector.serial = value;
      continue;
    }

    if (key == "user_id") {
      if (selector.user_id.has_value()) {
        error = "selector contains duplicate user_id key";
        return false;
      }
      selector.user_id = value;
      continue;
    }

    if (key == "index") {
      if (selector.index.has_value()) {
        error = "selector contains duplicate index key";
        return false;
      }
      std::size_t parsed_index = 0;
      if (!ParseNonNegativeIndex(value, parsed_index)) {
        error = "selector index must be a non-negative integer";
        return false;
      }
      selector.index = parsed_index;
      continue;
    }

    error = "selector key '" + key + "' is not supported (allowed: serial, user_id, index)";
    return false;
  }

  if (selector.serial.has_value() && selector.user_id.has_value()) {
    error = "selector cannot include both serial and user_id";
    return false;
  }

  if (!selector.serial.has_value() && !selector.user_id.has_value() &&
      !selector.index.has_value()) {
    error = "selector must include serial:<value>, user_id:<value>, or index:<n>";
    return false;
  }

  return true;
}

bool ResolveDeviceSelector(const std::vector<DeviceInfo>& devices, const DeviceSelector& selector,
                           DeviceInfo& selected, std::size_t& selected_index, std::string& error) {
  error.clear();
  if (devices.empty()) {
    error = "no connected cameras were discovered";
    return false;
  }

  std::vector<std::size_t> candidate_indices;
  candidate_indices.reserve(devices.size());
  for (std::size_t i = 0; i < devices.size(); ++i) {
    const DeviceInfo& device = devices[i];
    if (selector.serial.has_value() && device.serial != selector.serial.value()) {
      continue;
    }
    if (selector.user_id.has_value() && device.user_id != selector.user_id.value()) {
      continue;
    }
    candidate_indices.push_back(i);
  }

  if (candidate_indices.empty()) {
    if (selector.serial.has_value()) {
      error = "no device matched selector serial:" + selector.serial.value();
    } else if (selector.user_id.has_value()) {
      error = "no device matched selector user_id:" + selector.user_id.value();
    } else {
      error = "no candidate devices available for index selector";
    }
    return false;
  }

  if (selector.index.has_value()) {
    const std::size_t ordinal = selector.index.value();
    if (ordinal >= candidate_indices.size()) {
      error = "selector index " + std::to_string(ordinal) + " is out of range for " +
              std::to_string(candidate_indices.size()) + " candidate device(s)";
      return false;
    }
    selected_index = candidate_indices[ordinal];
    selected = devices[selected_index];
    return true;
  }

  if (candidate_indices.size() > 1U) {
    error = "selector matched multiple devices (" + std::to_string(candidate_indices.size()) +
            "); add index:<n> to disambiguate";
    return false;
  }

  selected_index = candidate_indices.front();
  selected = devices[selected_index];
  return true;
}

bool ResolveConnectedDevice(std::string_view selector_text, DeviceInfo& selected,
                            std::size_t& selected_index, std::string& error) {
  DeviceSelector selector;
  if (!ParseDeviceSelector(selector_text, selector, error)) {
    return false;
  }

  std::vector<DeviceInfo> devices;
  if (!EnumerateConnectedDevices(devices, error)) {
    return false;
  }

  return ResolveDeviceSelector(devices, selector, selected, selected_index, error);
}

} // namespace labops::backends::real_sdk
