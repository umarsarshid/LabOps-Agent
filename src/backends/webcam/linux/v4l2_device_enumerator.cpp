#include "backends/webcam/linux/v4l2_device_enumerator.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace labops::backends::webcam {

namespace {

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

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::optional<std::size_t> ParseVideoIndex(std::string_view device_name) {
  if (!StartsWith(device_name, "video")) {
    return std::nullopt;
  }
  const std::string_view suffix = device_name.substr(5);
  if (suffix.empty()) {
    return std::nullopt;
  }

  std::size_t parsed = 0;
  const char* begin = suffix.data();
  const char* end = begin + suffix.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<fs::path> DiscoverVideoNodes(std::string& error) {
  std::vector<fs::path> nodes;
  error.clear();

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator("/dev", ec)) {
    if (ec) {
      error = "failed to iterate /dev for V4L2 discovery: " + ec.message();
      return {};
    }
    if (!entry.is_character_file(ec)) {
      if (ec) {
        ec.clear();
      }
      continue;
    }

    const std::string name = entry.path().filename().string();
    if (!StartsWith(name, "video")) {
      continue;
    }
    nodes.push_back(entry.path());
  }

  std::sort(nodes.begin(), nodes.end(), [](const fs::path& left, const fs::path& right) {
    const std::optional<std::size_t> left_index = ParseVideoIndex(left.filename().string());
    const std::optional<std::size_t> right_index = ParseVideoIndex(right.filename().string());
    if (left_index.has_value() && right_index.has_value() &&
        left_index.value() != right_index.value()) {
      return left_index.value() < right_index.value();
    }
    return left.string() < right.string();
  });
  return nodes;
}

#if defined(__linux__)
bool QueryNode(const fs::path& node_path, WebcamDeviceInfo& device) {
  const int fd = open(node_path.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    return false;
  }

  v4l2_capability caps{};
  const int query_status = ioctl(fd, VIDIOC_QUERYCAP, &caps);
  close(fd);
  if (query_status != 0) {
    return false;
  }

  const std::uint32_t effective_caps =
      (caps.device_caps != 0U) ? caps.device_caps : caps.capabilities;
  const bool supports_video_capture = (effective_caps & V4L2_CAP_VIDEO_CAPTURE) != 0U ||
                                      (effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U;
  if (!supports_video_capture) {
    return false;
  }

  device = WebcamDeviceInfo{};
  device.device_id = node_path.filename().string();
  device.capture_index = ParseVideoIndex(device.device_id);
  device.friendly_name = Trim(reinterpret_cast<const char*>(caps.card));
  if (device.friendly_name.empty()) {
    device.friendly_name = device.device_id;
  }

  const std::string bus_info = Trim(reinterpret_cast<const char*>(caps.bus_info));
  if (!bus_info.empty()) {
    device.bus_info = bus_info;
  }
  return true;
}
#endif

} // namespace

bool EnumerateV4l2Devices(std::vector<WebcamDeviceInfo>& devices, std::string& error) {
  devices.clear();
  error.clear();

#if !defined(__linux__)
  (void)devices;
  error = "V4L2 enumeration is only supported on Linux";
  return false;
#else
  const std::vector<fs::path> nodes = DiscoverVideoNodes(error);
  if (!error.empty()) {
    return false;
  }

  for (const fs::path& node : nodes) {
    WebcamDeviceInfo device;
    if (!QueryNode(node, device)) {
      continue;
    }
    devices.push_back(std::move(device));
  }

  return true;
#endif
}

} // namespace labops::backends::webcam
