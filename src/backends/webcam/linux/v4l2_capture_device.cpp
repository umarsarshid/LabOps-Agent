#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace labops::backends::webcam {

namespace {

std::string Trim(std::string_view input) {
  std::size_t begin = 0U;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
    ++begin;
  }

  std::size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1U])) != 0) {
    --end;
  }
  return std::string(input.substr(begin, end - begin));
}

std::string FormatCapabilitiesHex(const std::uint32_t caps) {
  std::ostringstream out;
  out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << caps;
  return out.str();
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

#if defined(__linux__)
std::optional<std::uint32_t> ParseFourcc(std::string_view text) {
  if (text.size() != 4U) {
    return std::nullopt;
  }
  const std::uint32_t value =
      static_cast<std::uint32_t>(static_cast<unsigned char>(text[0])) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(text[1])) << 8U) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(text[2])) << 16U) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(text[3])) << 24U);
  return value;
}

std::string FourccToString(const std::uint32_t fourcc) {
  std::string out(4, ' ');
  out[0] = static_cast<char>(fourcc & 0xFFU);
  out[1] = static_cast<char>((fourcc >> 8U) & 0xFFU);
  out[2] = static_cast<char>((fourcc >> 16U) & 0xFFU);
  out[3] = static_cast<char>((fourcc >> 24U) & 0xFFU);
  return Trim(out);
}

bool TryFpsFromTimePerFrame(const v4l2_fract& time_per_frame, double& fps) {
  if (time_per_frame.numerator == 0U || time_per_frame.denominator == 0U) {
    return false;
  }
  const double computed = static_cast<double>(time_per_frame.denominator) /
                          static_cast<double>(time_per_frame.numerator);
  if (!std::isfinite(computed) || computed <= 0.0) {
    return false;
  }
  fps = computed;
  return true;
}

v4l2_fract BuildTimePerFrameFromFps(const double fps) {
  // Represent fractional FPS as ms period to keep integer math stable.
  constexpr std::uint32_t kBase = 1000U;
  const double scaled = fps * static_cast<double>(kBase);
  std::uint32_t denominator = static_cast<std::uint32_t>(std::llround(scaled));
  if (denominator == 0U) {
    denominator = 1U;
  }
  return v4l2_fract{.numerator = kBase, .denominator = denominator};
}
#endif

} // namespace

const char* ToString(const V4l2CaptureMethod method) {
  switch (method) {
  case V4l2CaptureMethod::kMmapStreaming:
    return "mmap_streaming";
  case V4l2CaptureMethod::kReadFallback:
    return "read_fallback";
  }
  return "mmap_streaming";
}

V4l2CaptureDevice::IoOps V4l2CaptureDevice::DefaultIoOps() {
  IoOps ops;
#if defined(__linux__)
  ops.open_fn = [](const char* path, const int flags) { return ::open(path, flags); };
  ops.close_fn = [](const int fd) { return ::close(fd); };
  ops.ioctl_fn = [](const int fd, const unsigned long request, void* arg) {
    return ::ioctl(fd, request, arg);
  };
#else
  ops.open_fn = [](const char* /*path*/, const int /*flags*/) {
    errno = ENOSYS;
    return -1;
  };
  ops.close_fn = [](const int /*fd*/) {
    errno = ENOSYS;
    return -1;
  };
  ops.ioctl_fn = [](const int /*fd*/, const unsigned long /*request*/, void* /*arg*/) {
    errno = ENOSYS;
    return -1;
  };
#endif
  return ops;
}

V4l2CaptureDevice::V4l2CaptureDevice(IoOps io_ops) : io_ops_(std::move(io_ops)) {}

V4l2CaptureDevice::~V4l2CaptureDevice() {
  std::string ignored_error;
  (void)Close(ignored_error);
}

bool V4l2CaptureDevice::Open(const std::string& device_path, V4l2OpenInfo& open_info,
                             std::string& error) {
  error.clear();
  open_info = V4l2OpenInfo{};

  if (device_path.empty()) {
    error = "device path cannot be empty";
    return false;
  }

  if (IsOpen()) {
    error = "device is already open: " + device_path_;
    return false;
  }

#if !defined(__linux__)
  (void)device_path;
  error = "V4L2 capture is only supported on Linux";
  return false;
#else
  if (!io_ops_.open_fn || !io_ops_.close_fn || !io_ops_.ioctl_fn) {
    error = "V4L2 IO operations are not configured";
    return false;
  }

  constexpr int kOpenFlags = O_RDWR | O_NONBLOCK;
  const int opened_fd = io_ops_.open_fn(device_path.c_str(), kOpenFlags);
  if (opened_fd < 0) {
    error = "failed to open V4L2 device '" + device_path + "': " + std::strerror(errno);
    return false;
  }

  v4l2_capability capability{};
  if (IoctlRetry(opened_fd, VIDIOC_QUERYCAP, &capability) != 0) {
    const int saved_errno = errno;
    (void)io_ops_.close_fn(opened_fd);
    error = "VIDIOC_QUERYCAP failed for '" + device_path + "': " + std::strerror(saved_errno);
    return false;
  }

  const std::uint32_t effective_caps =
      (capability.device_caps != 0U) ? capability.device_caps : capability.capabilities;
  if ((effective_caps & V4L2_CAP_VIDEO_CAPTURE) == 0U &&
      (effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0U) {
    (void)io_ops_.close_fn(opened_fd);
    error = "device '" + device_path +
            "' does not support video capture "
            "(capabilities=" +
            FormatCapabilitiesHex(effective_caps) + ")";
    return false;
  }

  V4l2CaptureMethod selected_method = V4l2CaptureMethod::kMmapStreaming;
  std::string selection_reason;
  if (!ChooseCaptureMethod(effective_caps, selected_method, selection_reason)) {
    (void)io_ops_.close_fn(opened_fd);
    error = "device '" + device_path + "' capture method selection failed: " + selection_reason +
            " (capabilities=" + FormatCapabilitiesHex(effective_caps) + ")";
    return false;
  }

  fd_ = opened_fd;
  device_path_ = device_path;
  effective_capabilities_ = effective_caps;
  buffer_type_ = (effective_caps & V4L2_CAP_VIDEO_CAPTURE) != 0U
                     ? V4L2_BUF_TYPE_VIDEO_CAPTURE
                     : V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  capture_method_ = selected_method;

  open_info.device_path = device_path_;
  open_info.driver_name = Trim(reinterpret_cast<const char*>(capability.driver));
  open_info.card_name = Trim(reinterpret_cast<const char*>(capability.card));
  open_info.effective_capabilities = effective_caps;
  open_info.capabilities_hex = FormatCapabilitiesHex(effective_caps);
  open_info.capture_method = selected_method;
  open_info.capture_method_reason = std::move(selection_reason);
  return true;
#endif
}

bool V4l2CaptureDevice::Close(std::string& error) {
  error.clear();
  if (!IsOpen()) {
    return true;
  }

  if (!io_ops_.close_fn) {
    error = "V4L2 close operation is not configured";
    return false;
  }

  if (io_ops_.close_fn(fd_) != 0) {
    error = "failed to close V4L2 device '" + device_path_ + "': " + std::strerror(errno);
    return false;
  }

  fd_ = -1;
  device_path_.clear();
  effective_capabilities_ = 0U;
  buffer_type_ = 0U;
  capture_method_ = V4l2CaptureMethod::kMmapStreaming;
  return true;
}

bool V4l2CaptureDevice::ApplyRequestedFormatBestEffort(const V4l2RequestedFormat& request,
                                                       V4l2ApplyResult& result,
                                                       std::string& error) {
  error.clear();
  result = V4l2ApplyResult{};

  if (!IsOpen()) {
    error = "device must be open before applying requested format";
    return false;
  }

#if !defined(__linux__)
  (void)request;
  error = "V4L2 capture is only supported on Linux";
  return false;
#else
  auto append_control = [&](std::string generic_key, std::string node_name,
                            std::string requested_value, std::string actual_value, bool supported,
                            bool applied, bool adjusted, std::string reason) {
    result.controls.push_back(V4l2AppliedControl{
        .generic_key = std::move(generic_key),
        .node_name = std::move(node_name),
        .requested_value = std::move(requested_value),
        .actual_value = std::move(actual_value),
        .supported = supported,
        .applied = applied,
        .adjusted = adjusted,
        .reason = std::move(reason),
    });
  };

  const bool has_format_request =
      request.width.has_value() || request.height.has_value() || request.pixel_format.has_value();
  if (has_format_request) {
    v4l2_format format{};
    format.type = buffer_type_;
    if (IoctlRetry(fd_, VIDIOC_G_FMT, &format) != 0) {
      // Proceed with S_FMT using zero-initialized structure while recording
      // that readback context is partial.
      format = v4l2_format{};
      format.type = buffer_type_;
    }

    std::optional<std::uint32_t> requested_fourcc;
    bool pixel_format_input_valid = true;
    std::string pixel_format_input_error;
    if (request.pixel_format.has_value()) {
      requested_fourcc = ParseFourcc(request.pixel_format.value());
      if (!requested_fourcc.has_value()) {
        pixel_format_input_valid = false;
        pixel_format_input_error = "pixel format must be 4 ASCII characters (example: MJPG)";
      }
    }

    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
      if (request.width.has_value()) {
        format.fmt.pix.width = request.width.value();
      }
      if (request.height.has_value()) {
        format.fmt.pix.height = request.height.value();
      }
      if (pixel_format_input_valid && requested_fourcc.has_value()) {
        format.fmt.pix.pixelformat = requested_fourcc.value();
      }
    } else {
      if (request.width.has_value()) {
        format.fmt.pix_mp.width = request.width.value();
      }
      if (request.height.has_value()) {
        format.fmt.pix_mp.height = request.height.value();
      }
      if (pixel_format_input_valid && requested_fourcc.has_value()) {
        format.fmt.pix_mp.pixelformat = requested_fourcc.value();
      }
    }

    std::optional<std::string> format_apply_error;
    if (IoctlRetry(fd_, VIDIOC_S_FMT, &format) != 0) {
      format_apply_error = std::strerror(errno);
    }

    auto actual_width = [&]() -> std::uint32_t {
      return buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE ? format.fmt.pix.width
                                                         : format.fmt.pix_mp.width;
    };
    auto actual_height = [&]() -> std::uint32_t {
      return buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE ? format.fmt.pix.height
                                                         : format.fmt.pix_mp.height;
    };
    auto actual_fourcc = [&]() -> std::uint32_t {
      return buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE ? format.fmt.pix.pixelformat
                                                         : format.fmt.pix_mp.pixelformat;
    };

    if (request.width.has_value()) {
      const std::string requested_text = std::to_string(request.width.value());
      if (format_apply_error.has_value()) {
        append_control("width", "V4L2_FMT.width", requested_text, "", false, false, false,
                       "VIDIOC_S_FMT rejected width: " + format_apply_error.value());
      } else {
        const std::string actual_text = std::to_string(actual_width());
        const bool adjusted = actual_text != requested_text;
        append_control("width", "V4L2_FMT.width", requested_text, actual_text, true, true, adjusted,
                       adjusted ? "driver adjusted width during format negotiation" : "");
      }
    }

    if (request.height.has_value()) {
      const std::string requested_text = std::to_string(request.height.value());
      if (format_apply_error.has_value()) {
        append_control("height", "V4L2_FMT.height", requested_text, "", false, false, false,
                       "VIDIOC_S_FMT rejected height: " + format_apply_error.value());
      } else {
        const std::string actual_text = std::to_string(actual_height());
        const bool adjusted = actual_text != requested_text;
        append_control("height", "V4L2_FMT.height", requested_text, actual_text, true, true,
                       adjusted,
                       adjusted ? "driver adjusted height during format negotiation" : "");
      }
    }

    if (request.pixel_format.has_value()) {
      const std::string requested_text = request.pixel_format.value();
      if (!pixel_format_input_valid) {
        append_control("pixel_format", "V4L2_FMT.pixelformat", requested_text, "", false, false,
                       false, pixel_format_input_error);
      } else if (format_apply_error.has_value()) {
        append_control("pixel_format", "V4L2_FMT.pixelformat", requested_text, "", false, false,
                       false, "VIDIOC_S_FMT rejected pixel format: " + format_apply_error.value());
      } else {
        const std::string actual_text = FourccToString(actual_fourcc());
        const bool adjusted = actual_text != requested_text;
        append_control("pixel_format", "V4L2_FMT.pixelformat", requested_text, actual_text, true,
                       true, adjusted,
                       adjusted ? "driver adjusted pixel format during format negotiation" : "");
      }
    }
  }

  if (request.fps.has_value()) {
    const std::string requested_text = FormatCompactDouble(request.fps.value());
    v4l2_streamparm stream_param{};
    stream_param.type = buffer_type_;

    if (IoctlRetry(fd_, VIDIOC_G_PARM, &stream_param) != 0) {
      append_control("fps", "V4L2_PARM.timeperframe", requested_text, "", false, false, false,
                     "VIDIOC_G_PARM failed: " + std::string(std::strerror(errno)));
      return true;
    }

    const bool supports_timeperframe =
        (stream_param.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) != 0U;
    if (!supports_timeperframe) {
      append_control("fps", "V4L2_PARM.timeperframe", requested_text, "", false, false, false,
                     "device does not advertise V4L2_CAP_TIMEPERFRAME");
      return true;
    }

    stream_param.parm.capture.timeperframe = BuildTimePerFrameFromFps(request.fps.value());
    if (IoctlRetry(fd_, VIDIOC_S_PARM, &stream_param) != 0) {
      append_control("fps", "V4L2_PARM.timeperframe", requested_text, "", false, false, false,
                     "VIDIOC_S_PARM failed: " + std::string(std::strerror(errno)));
      return true;
    }

    v4l2_streamparm readback_param{};
    readback_param.type = buffer_type_;
    if (IoctlRetry(fd_, VIDIOC_G_PARM, &readback_param) != 0) {
      append_control("fps", "V4L2_PARM.timeperframe", requested_text, "", false, false, false,
                     "VIDIOC_G_PARM readback failed: " + std::string(std::strerror(errno)));
      return true;
    }

    double actual_fps = 0.0;
    if (!TryFpsFromTimePerFrame(readback_param.parm.capture.timeperframe, actual_fps)) {
      append_control("fps", "V4L2_PARM.timeperframe", requested_text, "", false, false, false,
                     "driver returned invalid timeperframe readback");
      return true;
    }

    const std::string actual_text = FormatCompactDouble(actual_fps);
    const bool adjusted = std::fabs(actual_fps - request.fps.value()) > 1e-3;
    append_control("fps", "V4L2_PARM.timeperframe", requested_text, actual_text, true, true,
                   adjusted, adjusted ? "driver adjusted FPS to nearest supported value" : "");
  }

  return true;
#endif
}

bool V4l2CaptureDevice::IsOpen() const {
  return fd_ >= 0;
}

const std::string& V4l2CaptureDevice::DevicePath() const {
  return device_path_;
}

V4l2CaptureMethod V4l2CaptureDevice::CaptureMethod() const {
  return capture_method_;
}

bool V4l2CaptureDevice::ChooseCaptureMethod(const std::uint32_t effective_caps,
                                            V4l2CaptureMethod& capture_method,
                                            std::string& reason) {
  reason.clear();
#if !defined(__linux__)
  (void)effective_caps;
  (void)capture_method;
  reason = "V4L2 capture is only supported on Linux";
  return false;
#else
  const bool has_video_capture = (effective_caps & V4L2_CAP_VIDEO_CAPTURE) != 0U ||
                                 (effective_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0U;
  if (!has_video_capture) {
    reason = "device does not expose VIDEO_CAPTURE capability";
    return false;
  }

  if ((effective_caps & V4L2_CAP_STREAMING) != 0U) {
    capture_method = V4l2CaptureMethod::kMmapStreaming;
    reason = "selected mmap streaming (preferred)";
    return true;
  }

  if ((effective_caps & V4L2_CAP_READWRITE) != 0U) {
    capture_method = V4l2CaptureMethod::kReadFallback;
    reason = "selected read() fallback because streaming is unavailable";
    return true;
  }

  reason = "device does not support mmap streaming or read() capture";
  return false;
#endif
}

int V4l2CaptureDevice::IoctlRetry(const int fd, const unsigned long request, void* arg) const {
  if (!io_ops_.ioctl_fn) {
    errno = ENOSYS;
    return -1;
  }

  int status = -1;
  do {
    status = io_ops_.ioctl_fn(fd, request, arg);
  } while (status != 0 && errno == EINTR);
  return status;
}

} // namespace labops::backends::webcam
