#include "backends/webcam/device_selector.hpp"
#include "backends/webcam/linux/v4l2_device_enumerator.hpp"
#include "backends/webcam/opencv_webcam_impl.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace labops::backends::webcam {

namespace {

struct FixtureRow {
  std::string device_id;
  std::string friendly_name;
  std::string bus_info;
  std::optional<std::size_t> capture_index;
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

std::vector<std::string> SplitSimpleCsvLine(const std::string& line) {
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
  for (char c : raw) {
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

std::size_t ResolveProbeLimit() {
  constexpr std::size_t kDefaultProbeLimit = 8;
  const char* env = std::getenv("LABOPS_WEBCAM_MAX_PROBE_INDEX");
  if (env == nullptr || *env == '\0') {
    return kDefaultProbeLimit;
  }

  std::size_t parsed = 0;
  if (!ParseNonNegativeIndex(env, parsed)) {
    return kDefaultProbeLimit;
  }
  return parsed;
}

bool ParseFixtureRow(const std::string& line, std::size_t line_number, FixtureRow& row,
                     std::string& error) {
  const std::vector<std::string> fields = SplitSimpleCsvLine(line);
  if (fields.size() < 2U) {
    error = "webcam fixture parse error at line " + std::to_string(line_number) +
            ": expected at least 2 CSV fields (device_id,friendly_name)";
    return false;
  }
  row.device_id = fields[0];
  row.friendly_name = fields[1];
  row.bus_info = fields.size() >= 3U ? fields[2] : "";
  row.capture_index.reset();
  if (ToLower(row.device_id) == "device_id" && ToLower(row.friendly_name) == "friendly_name") {
    return true;
  }
  if (fields.size() >= 4U && !fields[3].empty()) {
    std::size_t parsed_index = 0;
    if (!ParseNonNegativeIndex(fields[3], parsed_index)) {
      error = "webcam fixture parse error at line " + std::to_string(line_number) +
              ": capture_index must be a non-negative integer";
      return false;
    }
    row.capture_index = parsed_index;
  }
  return true;
}

bool LooksLikeHeader(const FixtureRow& row) {
  return ToLower(row.device_id) == "device_id" && ToLower(row.friendly_name) == "friendly_name";
}

bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const std::string lower_haystack = ToLower(std::string(haystack));
  const std::string lower_needle = ToLower(std::string(needle));
  return lower_haystack.find(lower_needle) != std::string::npos;
}

void StableSortDevices(std::vector<WebcamDeviceInfo>& devices) {
  std::sort(devices.begin(), devices.end(),
            [](const WebcamDeviceInfo& left, const WebcamDeviceInfo& right) {
              if (left.capture_index.has_value() && right.capture_index.has_value() &&
                  left.capture_index.value() != right.capture_index.value()) {
                return left.capture_index.value() < right.capture_index.value();
              }
              if (left.device_id != right.device_id) {
                return left.device_id < right.device_id;
              }
              if (left.friendly_name != right.friendly_name) {
                return left.friendly_name < right.friendly_name;
              }
              return left.bus_info.value_or("") < right.bus_info.value_or("");
            });
}

WebcamDeviceInfo MapFixtureRowToDevice(const FixtureRow& row) {
  WebcamDeviceInfo device;
  device.device_id = row.device_id;
  device.friendly_name = row.friendly_name;
  const std::string bus_info = Trim(row.bus_info);
  if (!bus_info.empty()) {
    device.bus_info = bus_info;
  }
  device.capture_index = row.capture_index;
  return device;
}

WebcamDeviceInfo MakeOpenCvDiscoveredDevice(const std::size_t index) {
  WebcamDeviceInfo device;
  device.device_id = "opencv-index-" + std::to_string(index);
  device.friendly_name = "OpenCV Camera " + std::to_string(index);
  device.bus_info = "opencv:index:" + std::to_string(index);
  device.capture_index = index;
  return device;
}

} // namespace

const char* ToString(const WebcamSelectionRule rule) {
  switch (rule) {
  case WebcamSelectionRule::kId:
    return "id";
  case WebcamSelectionRule::kIndex:
    return "index";
  case WebcamSelectionRule::kNameContains:
    return "name_contains";
  case WebcamSelectionRule::kDefaultDevice:
    return "default_index_0";
  }
  return "default_index_0";
}

bool ParseWebcamDeviceSelector(std::string_view selector_text, WebcamDeviceSelector& selector,
                               std::string& error) {
  selector = WebcamDeviceSelector{};
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

    if (key == "id") {
      if (selector.id.has_value()) {
        error = "selector contains duplicate id key";
        return false;
      }
      selector.id = value;
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

    if (key == "name_contains") {
      if (selector.name_contains.has_value()) {
        error = "selector contains duplicate name_contains key";
        return false;
      }
      selector.name_contains = value;
      continue;
    }

    error = "selector key '" + key + "' is not supported (allowed: id, index, name_contains)";
    return false;
  }

  if (!selector.id.has_value() && !selector.index.has_value() &&
      !selector.name_contains.has_value()) {
    error = "selector must include id:<value>, index:<n>, or name_contains:<substring>";
    return false;
  }
  return true;
}

bool EnumerateConnectedDevices(std::vector<WebcamDeviceInfo>& devices, std::string& error) {
  devices.clear();
  error.clear();

  const char* fixture_path = std::getenv("LABOPS_WEBCAM_DEVICE_FIXTURE");
  if (fixture_path != nullptr && *fixture_path != '\0') {
    std::ifstream input(fs::path(fixture_path), std::ios::binary);
    if (!input) {
      error = "unable to open LABOPS_WEBCAM_DEVICE_FIXTURE file: " + std::string(fixture_path);
      return false;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      const std::string trimmed = Trim(line);
      if (trimmed.empty() || trimmed.rfind('#', 0U) == 0U) {
        continue;
      }

      FixtureRow row;
      if (!ParseFixtureRow(trimmed, line_number, row, error)) {
        return false;
      }
      if (LooksLikeHeader(row)) {
        continue;
      }

      const WebcamDeviceInfo mapped = MapFixtureRowToDevice(row);
      if (mapped.device_id.empty()) {
        error = "webcam fixture parse error at line " + std::to_string(line_number) +
                ": device_id must be non-empty";
        return false;
      }
      if (mapped.friendly_name.empty()) {
        error = "webcam fixture parse error at line " + std::to_string(line_number) +
                ": friendly_name must be non-empty";
        return false;
      }
      devices.push_back(mapped);
    }
  } else {
#if defined(__linux__)
    // Prefer native Linux discovery first so list-devices and selector flows
    // report actual V4L2 device identities even when OpenCV is also enabled.
    std::vector<WebcamDeviceInfo> v4l2_devices;
    std::string v4l2_error;
    if (EnumerateV4l2Devices(v4l2_devices, v4l2_error) && !v4l2_devices.empty()) {
      devices = std::move(v4l2_devices);
    } else
#endif
    {
      const std::size_t probe_limit = ResolveProbeLimit();
      const std::vector<std::size_t> discovered_indices =
          OpenCvWebcamImpl::EnumerateDeviceIndices(probe_limit);
      devices.reserve(discovered_indices.size());
      for (const std::size_t index : discovered_indices) {
        devices.push_back(MakeOpenCvDiscoveredDevice(index));
      }
    }
  }

  StableSortDevices(devices);
  return true;
}

bool ResolveWebcamDeviceSelector(const std::vector<WebcamDeviceInfo>& devices,
                                 const WebcamDeviceSelector& selector,
                                 WebcamSelectionResult& resolved, std::string& error) {
  error.clear();
  if (devices.empty()) {
    error = "no webcam devices were discovered";
    return false;
  }

  std::vector<WebcamDeviceInfo> sorted_devices = devices;
  StableSortDevices(sorted_devices);

  if (selector.id.has_value()) {
    for (std::size_t i = 0; i < sorted_devices.size(); ++i) {
      if (sorted_devices[i].device_id == selector.id.value()) {
        resolved.device = sorted_devices[i];
        resolved.index = i;
        resolved.rule = WebcamSelectionRule::kId;
        return true;
      }
    }
    error = "no webcam device matched selector id:" + selector.id.value();
    return false;
  }

  if (selector.index.has_value()) {
    const std::size_t index = selector.index.value();
    if (index >= sorted_devices.size()) {
      error = "webcam selector index " + std::to_string(index) + " is out of range for " +
              std::to_string(sorted_devices.size()) + " discovered device(s)";
      return false;
    }
    resolved.device = sorted_devices[index];
    resolved.index = index;
    resolved.rule = WebcamSelectionRule::kIndex;
    return true;
  }

  if (selector.name_contains.has_value()) {
    for (std::size_t i = 0; i < sorted_devices.size(); ++i) {
      if (!ContainsCaseInsensitive(sorted_devices[i].friendly_name,
                                   selector.name_contains.value())) {
        continue;
      }
      resolved.device = sorted_devices[i];
      resolved.index = i;
      resolved.rule = WebcamSelectionRule::kNameContains;
      return true;
    }
    error = "no webcam device matched selector name_contains:" + selector.name_contains.value();
    return false;
  }

  resolved.device = sorted_devices[0];
  resolved.index = 0;
  resolved.rule = WebcamSelectionRule::kDefaultDevice;
  return true;
}

} // namespace labops::backends::webcam
