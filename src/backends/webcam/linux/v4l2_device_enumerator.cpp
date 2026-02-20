#include "backends/webcam/linux/v4l2_device_enumerator.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
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

constexpr std::size_t kMaxFpsProbeSizesPerFormat = 32U;

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

std::string FormatCompactDouble(const double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  std::string text = out.str();
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text.empty() ? "0" : text;
}

void UpdateRange(std::optional<double>& minimum, std::optional<double>& maximum,
                 const double value) {
  if (!minimum.has_value() || value < minimum.value()) {
    minimum = value;
  }
  if (!maximum.has_value() || value > maximum.value()) {
    maximum = value;
  }
}

void UpdateStep(std::optional<double>& step, const double value) {
  if (value <= 0.0) {
    return;
  }
  if (!step.has_value() || value < step.value()) {
    step = value;
  }
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
struct V4l2DiscoveryAccumulator {
  std::set<std::string> pixel_formats;
  std::optional<double> width_min;
  std::optional<double> width_max;
  std::optional<double> width_step;
  std::optional<double> height_min;
  std::optional<double> height_max;
  std::optional<double> height_step;
  std::set<double> fps_discrete_values;
  std::optional<double> fps_min;
  std::optional<double> fps_max;
};

int IoctlRetry(const int fd, const unsigned long request, void* arg) {
  int status = 0;
  do {
    status = ioctl(fd, request, arg);
  } while (status != 0 && errno == EINTR);
  return status;
}

std::string FourccToString(const std::uint32_t fourcc) {
  std::string text(4, ' ');
  text[0] = static_cast<char>(fourcc & 0xFFU);
  text[1] = static_cast<char>((fourcc >> 8U) & 0xFFU);
  text[2] = static_cast<char>((fourcc >> 16U) & 0xFFU);
  text[3] = static_cast<char>((fourcc >> 24U) & 0xFFU);

  bool printable = true;
  for (const char c : text) {
    const unsigned char ascii = static_cast<unsigned char>(c);
    if (ascii < 32U || ascii > 126U) {
      printable = false;
      break;
    }
  }

  if (printable) {
    const std::string trimmed = Trim(text);
    if (!trimmed.empty()) {
      return trimmed;
    }
  }

  std::ostringstream out;
  out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << fourcc;
  return out.str();
}

bool TryFpsFromFraction(const std::uint32_t numerator, const std::uint32_t denominator,
                        double& fps) {
  if (numerator == 0U || denominator == 0U) {
    return false;
  }
  fps = static_cast<double>(denominator) / static_cast<double>(numerator);
  return std::isfinite(fps) && fps > 0.0;
}

void EnumerateFrameIntervals(const int fd, const std::uint32_t pixel_format,
                             const std::uint32_t width, const std::uint32_t height,
                             V4l2DiscoveryAccumulator& accumulator) {
  v4l2_frmivalenum interval{};
  interval.pixel_format = pixel_format;
  interval.width = width;
  interval.height = height;

  for (std::uint32_t index = 0U;; ++index) {
    interval.index = index;
    if (IoctlRetry(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) != 0) {
      break;
    }

    if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
      double fps = 0.0;
      if (TryFpsFromFraction(interval.discrete.numerator, interval.discrete.denominator, fps)) {
        accumulator.fps_discrete_values.insert(fps);
        UpdateRange(accumulator.fps_min, accumulator.fps_max, fps);
      }
      continue;
    }

    if (interval.type == V4L2_FRMIVAL_TYPE_STEPWISE ||
        interval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
      double min_fps = 0.0;
      // min frame interval => max FPS
      if (TryFpsFromFraction(interval.stepwise.min.numerator, interval.stepwise.min.denominator,
                             min_fps)) {
        UpdateRange(accumulator.fps_min, accumulator.fps_max, min_fps);
      }

      double max_fps = 0.0;
      // max frame interval => min FPS
      if (TryFpsFromFraction(interval.stepwise.max.numerator, interval.stepwise.max.denominator,
                             max_fps)) {
        UpdateRange(accumulator.fps_min, accumulator.fps_max, max_fps);
      }
      break;
    }
  }
}

void EnumerateFrameSizesForFormat(const int fd, const std::uint32_t pixel_format,
                                  V4l2DiscoveryAccumulator& accumulator) {
  v4l2_frmsizeenum frame_size{};
  frame_size.pixel_format = pixel_format;
  std::size_t interval_probe_count = 0U;

  for (std::uint32_t index = 0U;; ++index) {
    frame_size.index = index;
    if (IoctlRetry(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) != 0) {
      break;
    }

    if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      const double width = static_cast<double>(frame_size.discrete.width);
      const double height = static_cast<double>(frame_size.discrete.height);
      UpdateRange(accumulator.width_min, accumulator.width_max, width);
      UpdateRange(accumulator.height_min, accumulator.height_max, height);
      if (interval_probe_count < kMaxFpsProbeSizesPerFormat) {
        EnumerateFrameIntervals(fd, pixel_format, frame_size.discrete.width,
                                frame_size.discrete.height, accumulator);
        ++interval_probe_count;
      }
      continue;
    }

    if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
        frame_size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
      const auto& stepwise = frame_size.stepwise;
      UpdateRange(accumulator.width_min, accumulator.width_max,
                  static_cast<double>(stepwise.min_width));
      UpdateRange(accumulator.width_min, accumulator.width_max,
                  static_cast<double>(stepwise.max_width));
      UpdateStep(accumulator.width_step, static_cast<double>(stepwise.step_width));
      UpdateRange(accumulator.height_min, accumulator.height_max,
                  static_cast<double>(stepwise.min_height));
      UpdateRange(accumulator.height_min, accumulator.height_max,
                  static_cast<double>(stepwise.max_height));
      UpdateStep(accumulator.height_step, static_cast<double>(stepwise.step_height));

      // Stepwise sizing can represent many combinations. Probe FPS using low/high
      // anchors for best-effort range evidence.
      EnumerateFrameIntervals(fd, pixel_format, stepwise.min_width, stepwise.min_height,
                              accumulator);
      if (stepwise.max_width != stepwise.min_width || stepwise.max_height != stepwise.min_height) {
        EnumerateFrameIntervals(fd, pixel_format, stepwise.max_width, stepwise.max_height,
                                accumulator);
      }
      break;
    }
  }
}

void EnumerateFormatCapabilitiesForType(const int fd, const std::uint32_t buffer_type,
                                        V4l2DiscoveryAccumulator& accumulator) {
  v4l2_fmtdesc format_desc{};
  format_desc.type = buffer_type;

  for (std::uint32_t index = 0U;; ++index) {
    format_desc.index = index;
    if (IoctlRetry(fd, VIDIOC_ENUM_FMT, &format_desc) != 0) {
      break;
    }

    accumulator.pixel_formats.insert(FourccToString(format_desc.pixelformat));
    EnumerateFrameSizesForFormat(fd, format_desc.pixelformat, accumulator);
  }
}

std::optional<WebcamControlSpec> QueryControlSpec(const int fd, const std::uint32_t control_id) {
  v4l2_queryctrl query{};
  query.id = control_id;
  if (IoctlRetry(fd, VIDIOC_QUERYCTRL, &query) != 0) {
    return std::nullopt;
  }
  if ((query.flags & V4L2_CTRL_FLAG_DISABLED) != 0U) {
    return std::nullopt;
  }

  WebcamControlSpec spec;
  spec.read_only = (query.flags & V4L2_CTRL_FLAG_READ_ONLY) != 0U;

  auto apply_integer_range = [&]() {
    spec.range.min = static_cast<double>(query.minimum);
    spec.range.max = static_cast<double>(query.maximum);
    if (query.step > 0) {
      spec.range.step = static_cast<double>(query.step);
    }
  };

  switch (query.type) {
  case V4L2_CTRL_TYPE_BOOLEAN:
    spec.value_type = WebcamControlValueType::kBoolean;
    spec.range.min = 0.0;
    spec.range.max = 1.0;
    spec.range.step = 1.0;
    break;
  case V4L2_CTRL_TYPE_MENU:
#if defined(V4L2_CTRL_TYPE_INTEGER_MENU)
  case V4L2_CTRL_TYPE_INTEGER_MENU:
#endif
    spec.value_type = WebcamControlValueType::kEnum;
    for (std::int64_t value = static_cast<std::int64_t>(query.minimum);
         value <= static_cast<std::int64_t>(query.maximum); ++value) {
      v4l2_querymenu menu{};
      menu.id = control_id;
      menu.index = static_cast<std::uint32_t>(value);
      if (IoctlRetry(fd, VIDIOC_QUERYMENU, &menu) != 0) {
        continue;
      }
#if defined(V4L2_CTRL_TYPE_INTEGER_MENU)
      if (query.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
        spec.enum_values.push_back(std::to_string(menu.value));
        continue;
      }
#endif
      std::string label = Trim(reinterpret_cast<const char*>(menu.name));
      if (label.empty()) {
        label = std::to_string(value);
      }
      spec.enum_values.push_back(std::move(label));
    }
    if (spec.enum_values.empty()) {
      spec.value_type = WebcamControlValueType::kInteger;
      apply_integer_range();
    }
    break;
  case V4L2_CTRL_TYPE_INTEGER:
  case V4L2_CTRL_TYPE_BITMASK:
#if defined(V4L2_CTRL_TYPE_INTEGER64)
  case V4L2_CTRL_TYPE_INTEGER64:
#endif
    spec.value_type = WebcamControlValueType::kInteger;
    apply_integer_range();
    break;
  default:
    spec.value_type = WebcamControlValueType::kInteger;
    apply_integer_range();
    break;
  }

  return spec;
}

void TryAddControlSpec(const int fd, const std::uint32_t v4l2_control_id,
                       const WebcamControlId control_id, SupportedControls& controls) {
  if (const std::optional<WebcamControlSpec> spec = QueryControlSpec(fd, v4l2_control_id);
      spec.has_value()) {
    controls[control_id] = spec.value();
  }
}

void PopulateFormatAndRateControls(const int fd, const std::uint32_t effective_caps,
                                   SupportedControls& controls) {
  V4l2DiscoveryAccumulator accumulator;
  if ((effective_caps & V4L2_CAP_VIDEO_CAPTURE) != 0U) {
    EnumerateFormatCapabilitiesForType(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, accumulator);
  }
  if ((effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U) {
    EnumerateFormatCapabilitiesForType(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, accumulator);
  }

  if (accumulator.width_min.has_value() && accumulator.width_max.has_value()) {
    WebcamControlSpec width_spec;
    width_spec.value_type = WebcamControlValueType::kInteger;
    width_spec.range.min = accumulator.width_min;
    width_spec.range.max = accumulator.width_max;
    width_spec.range.step = accumulator.width_step;
    controls[WebcamControlId::kWidth] = std::move(width_spec);
  }

  if (accumulator.height_min.has_value() && accumulator.height_max.has_value()) {
    WebcamControlSpec height_spec;
    height_spec.value_type = WebcamControlValueType::kInteger;
    height_spec.range.min = accumulator.height_min;
    height_spec.range.max = accumulator.height_max;
    height_spec.range.step = accumulator.height_step;
    controls[WebcamControlId::kHeight] = std::move(height_spec);
  }

  if (!accumulator.pixel_formats.empty()) {
    WebcamControlSpec pixel_format_spec;
    pixel_format_spec.value_type = WebcamControlValueType::kEnum;
    pixel_format_spec.enum_values.assign(accumulator.pixel_formats.begin(),
                                         accumulator.pixel_formats.end());
    controls[WebcamControlId::kPixelFormat] = std::move(pixel_format_spec);
  }

  if (!accumulator.fps_discrete_values.empty() || accumulator.fps_min.has_value() ||
      accumulator.fps_max.has_value()) {
    WebcamControlSpec fps_spec;
    fps_spec.value_type = WebcamControlValueType::kFloat;
    fps_spec.range.min = accumulator.fps_min;
    fps_spec.range.max = accumulator.fps_max;
    for (const double fps : accumulator.fps_discrete_values) {
      fps_spec.enum_values.push_back(FormatCompactDouble(fps));
    }
    controls[WebcamControlId::kFps] = std::move(fps_spec);
  }
}

void PopulateBestEffortQueryControls(const int fd, SupportedControls& controls) {
#if defined(V4L2_CID_EXPOSURE_ABSOLUTE)
  TryAddControlSpec(fd, V4L2_CID_EXPOSURE_ABSOLUTE, WebcamControlId::kExposure, controls);
#endif
#if defined(V4L2_CID_GAIN)
  TryAddControlSpec(fd, V4L2_CID_GAIN, WebcamControlId::kGain, controls);
#endif
#if defined(V4L2_CID_EXPOSURE_AUTO)
  TryAddControlSpec(fd, V4L2_CID_EXPOSURE_AUTO, WebcamControlId::kAutoExposure, controls);
#endif
}

bool QueryNode(const fs::path& node_path, WebcamDeviceInfo& device) {
  const int fd = open(node_path.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    return false;
  }

  v4l2_capability caps{};
  const int query_status = IoctlRetry(fd, VIDIOC_QUERYCAP, &caps);
  if (query_status != 0) {
    close(fd);
    return false;
  }

  const std::uint32_t effective_caps =
      (caps.device_caps != 0U) ? caps.device_caps : caps.capabilities;
  const bool supports_video_capture = (effective_caps & V4L2_CAP_VIDEO_CAPTURE) != 0U ||
                                      (effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U;
  if (!supports_video_capture) {
    close(fd);
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

  // Build the normalized supported-controls snapshot used by discovery reports.
  PopulateFormatAndRateControls(fd, effective_caps, device.supported_controls);
  PopulateBestEffortQueryControls(fd, device.supported_controls);

  close(fd);
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
