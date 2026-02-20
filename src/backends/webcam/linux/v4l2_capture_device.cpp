#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <cctype>
#include <cerrno>
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
  while (begin < input.size() &&
         std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
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
  capture_method_ = V4l2CaptureMethod::kMmapStreaming;
  return true;
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
