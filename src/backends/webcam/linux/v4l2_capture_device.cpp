#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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
  ops.poll_fn = [](struct ::pollfd* fds, const unsigned long nfds, const int timeout_ms) {
    return ::poll(fds, static_cast<nfds_t>(nfds), timeout_ms);
  };
  ops.mmap_fn = [](void* addr, const std::size_t length, const int prot, const int flags,
                   const int fd, const std::int64_t offset) -> void* {
    return ::mmap(addr, length, prot, flags, fd, static_cast<off_t>(offset));
  };
  ops.munmap_fn = [](void* addr, const std::size_t length) { return ::munmap(addr, length); };
  ops.steady_now_fn = [] { return std::chrono::steady_clock::now(); };
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
  ops.poll_fn = [](struct ::pollfd* /*fds*/, const unsigned long /*nfds*/,
                   const int /*timeout_ms*/) {
    errno = ENOSYS;
    return -1;
  };
  ops.mmap_fn = [](void* /*addr*/, const std::size_t /*length*/, const int /*prot*/,
                   const int /*flags*/, const int /*fd*/, const std::int64_t /*offset*/) -> void* {
    errno = ENOSYS;
    return nullptr;
  };
  ops.munmap_fn = [](void* /*addr*/, const std::size_t /*length*/) {
    errno = ENOSYS;
    return -1;
  };
  ops.steady_now_fn = [] { return std::chrono::steady_clock::now(); };
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

  std::string stream_error;
  if (!StopStreaming(stream_error)) {
    error = "failed to stop V4L2 streaming before close: " + stream_error;
    return false;
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
  mmap_buffers_.clear();
  mmap_buffers_allocated_ = false;
  streaming_ = false;
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

bool V4l2CaptureDevice::StartMmapStreaming(const std::size_t requested_buffer_count,
                                           V4l2StreamStartInfo& stream_info, std::string& error) {
  error.clear();
  stream_info = V4l2StreamStartInfo{};

  if (!IsOpen()) {
    error = "device must be open before starting streaming";
    return false;
  }

#if !defined(__linux__)
  (void)requested_buffer_count;
  error = "V4L2 capture is only supported on Linux";
  return false;
#else
  if (capture_method_ != V4l2CaptureMethod::kMmapStreaming) {
    error = "mmap streaming is unavailable for this device (selected capture method: " +
            std::string(ToString(capture_method_)) + ")";
    return false;
  }
  if (!io_ops_.mmap_fn || !io_ops_.munmap_fn) {
    error = "V4L2 mmap operations are not configured";
    return false;
  }
  if (streaming_) {
    error = "V4L2 stream is already running";
    return false;
  }

  const std::size_t buffer_target = requested_buffer_count == 0U ? 4U : requested_buffer_count;
  if (buffer_target > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    error = "requested buffer count is out of range";
    return false;
  }

  if (!mmap_buffers_.empty() || mmap_buffers_allocated_) {
    std::string cleanup_error;
    if (!StopStreaming(cleanup_error)) {
      error = "failed to reset prior stream state: " + cleanup_error;
      return false;
    }
  }

  auto release_allocated_buffers = [&](std::string& release_error) -> bool {
    bool success = true;
    for (const MmapBuffer& buffer : mmap_buffers_) {
      if (buffer.address == nullptr || buffer.length == 0U) {
        continue;
      }
      if (io_ops_.munmap_fn(buffer.address, buffer.length) != 0 && success) {
        release_error = "failed to munmap V4L2 buffer: " + std::string(std::strerror(errno));
        success = false;
      }
    }
    mmap_buffers_.clear();

    if (mmap_buffers_allocated_) {
      v4l2_requestbuffers release_req{};
      release_req.count = 0U;
      release_req.type = buffer_type_;
      release_req.memory = V4L2_MEMORY_MMAP;
      if (IoctlRetry(fd_, VIDIOC_REQBUFS, &release_req) != 0 && success) {
        release_error = "failed to release V4L2 mmap buffers: " + std::string(std::strerror(errno));
        success = false;
      }
      mmap_buffers_allocated_ = false;
    }
    streaming_ = false;
    return success;
  };

  v4l2_requestbuffers req{};
  req.count = static_cast<std::uint32_t>(buffer_target);
  req.type = buffer_type_;
  req.memory = V4L2_MEMORY_MMAP;
  if (IoctlRetry(fd_, VIDIOC_REQBUFS, &req) != 0) {
    error = "VIDIOC_REQBUFS failed: " + std::string(std::strerror(errno));
    return false;
  }
  if (req.count == 0U) {
    error = "VIDIOC_REQBUFS returned zero buffers";
    return false;
  }
  mmap_buffers_allocated_ = true;
  mmap_buffers_.reserve(req.count);

  constexpr std::size_t kMaxPlanes = 8U;
  for (std::uint32_t i = 0U; i < req.count; ++i) {
    v4l2_buffer query{};
    query.type = buffer_type_;
    query.memory = V4L2_MEMORY_MMAP;
    query.index = i;
    std::array<v4l2_plane, kMaxPlanes> query_planes{};
    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      query.m.planes = query_planes.data();
      query.length = static_cast<decltype(query.length)>(query_planes.size());
    }

    if (IoctlRetry(fd_, VIDIOC_QUERYBUF, &query) != 0) {
      std::string cleanup_error;
      (void)release_allocated_buffers(cleanup_error);
      error = "VIDIOC_QUERYBUF failed for buffer " + std::to_string(i) + ": " +
              std::string(std::strerror(errno));
      return false;
    }

    std::size_t buffer_length = 0U;
    std::uint32_t buffer_offset = 0U;
    std::size_t plane_count = 0U;
    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
      buffer_length = query.length;
      buffer_offset = query.m.offset;
    } else {
      plane_count = query.length;
      if (plane_count == 0U) {
        std::string cleanup_error;
        (void)release_allocated_buffers(cleanup_error);
        error = "VIDIOC_QUERYBUF returned zero planes for buffer " + std::to_string(i);
        return false;
      }
      buffer_length = query_planes[0].length;
      buffer_offset = query_planes[0].m.mem_offset;
    }
    if (buffer_length == 0U) {
      std::string cleanup_error;
      (void)release_allocated_buffers(cleanup_error);
      error = "VIDIOC_QUERYBUF returned empty buffer length for buffer " + std::to_string(i);
      return false;
    }

    void* mapped = io_ops_.mmap_fn(nullptr, buffer_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
                                   static_cast<std::int64_t>(buffer_offset));
    if (mapped == MAP_FAILED) {
      std::string cleanup_error;
      (void)release_allocated_buffers(cleanup_error);
      error =
          "mmap failed for buffer " + std::to_string(i) + ": " + std::string(std::strerror(errno));
      return false;
    }

    mmap_buffers_.push_back(MmapBuffer{.address = mapped, .length = buffer_length});

    v4l2_buffer qbuf{};
    qbuf.type = buffer_type_;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = i;
    std::array<v4l2_plane, kMaxPlanes> qbuf_planes{};
    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      qbuf.m.planes = qbuf_planes.data();
      qbuf.length = static_cast<decltype(qbuf.length)>(plane_count);
    }
    if (IoctlRetry(fd_, VIDIOC_QBUF, &qbuf) != 0) {
      std::string cleanup_error;
      (void)release_allocated_buffers(cleanup_error);
      error = "VIDIOC_QBUF failed for buffer " + std::to_string(i) + ": " +
              std::string(std::strerror(errno));
      return false;
    }
  }

  int stream_type = static_cast<int>(buffer_type_);
  if (IoctlRetry(fd_, VIDIOC_STREAMON, &stream_type) != 0) {
    std::string cleanup_error;
    (void)release_allocated_buffers(cleanup_error);
    error = "VIDIOC_STREAMON failed: " + std::string(std::strerror(errno));
    return false;
  }

  streaming_ = true;
  stream_info.buffer_type = buffer_type_;
  stream_info.buffer_count = mmap_buffers_.size();
  return true;
#endif
}

bool V4l2CaptureDevice::PullFrames(std::chrono::milliseconds duration, std::uint64_t& next_frame_id,
                                   std::vector<V4l2FrameSample>& frames, std::string& error) {
  error.clear();
  frames.clear();

  if (duration < std::chrono::milliseconds::zero()) {
    error = "pull_frames duration cannot be negative";
    return false;
  }
  if (duration == std::chrono::milliseconds::zero()) {
    return true;
  }

  if (!IsOpen()) {
    error = "device must be open before pull_frames";
    return false;
  }
  if (!streaming_) {
    error = "device must be streaming before pull_frames";
    return false;
  }

#if !defined(__linux__)
  (void)next_frame_id;
  error = "V4L2 capture is only supported on Linux";
  return false;
#else
  if (!io_ops_.poll_fn) {
    error = "V4L2 poll operation is not configured";
    return false;
  }

  const auto now_steady = [&]() {
    if (io_ops_.steady_now_fn) {
      return io_ops_.steady_now_fn();
    }
    return std::chrono::steady_clock::now();
  };

  constexpr std::chrono::milliseconds kPollBudget(200);
  const auto deadline = now_steady() + duration;

  while (now_steady() < deadline) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now_steady());
    const auto timeout_budget = std::max<std::chrono::milliseconds>(
        std::chrono::milliseconds(1), std::min(kPollBudget, remaining));
    const int timeout_ms = timeout_budget.count() > static_cast<decltype(timeout_budget.count())>(
                                                        std::numeric_limits<int>::max())
                               ? std::numeric_limits<int>::max()
                               : static_cast<int>(timeout_budget.count());

    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN | POLLPRI | POLLERR;
    const int poll_status = io_ops_.poll_fn(&pfd, 1U, timeout_ms);
    const auto outcome_ts = now_steady();

    if (poll_status == 0) {
      frames.push_back(V4l2FrameSample{
          .frame_id = next_frame_id++,
          .captured_at_steady = outcome_ts,
          .size_bytes = 0U,
          .outcome = V4l2FrameOutcome::kTimeout,
      });
      continue;
    }

    if (poll_status < 0) {
      if (errno == EINTR) {
        continue;
      }
      error = "poll failed while waiting for frame data: " + std::string(std::strerror(errno));
      return false;
    }

    v4l2_buffer dequeue{};
    dequeue.type = buffer_type_;
    dequeue.memory = V4L2_MEMORY_MMAP;
    std::array<v4l2_plane, 8U> dequeue_planes{};
    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      dequeue.m.planes = dequeue_planes.data();
      dequeue.length = static_cast<decltype(dequeue.length)>(dequeue_planes.size());
    }

    if (IoctlRetry(fd_, VIDIOC_DQBUF, &dequeue) != 0) {
      if (errno == EAGAIN) {
        frames.push_back(V4l2FrameSample{
            .frame_id = next_frame_id++,
            .captured_at_steady = outcome_ts,
            .size_bytes = 0U,
            .outcome = V4l2FrameOutcome::kTimeout,
        });
        continue;
      }
      error = "VIDIOC_DQBUF failed: " + std::string(std::strerror(errno));
      return false;
    }

    std::uint32_t bytes_used = 0U;
    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
      bytes_used = dequeue.bytesused;
    } else if (dequeue.length > 0U && dequeue.m.planes != nullptr) {
      bytes_used = dequeue.m.planes[0].bytesused;
    }

    const bool flagged_error = (dequeue.flags & V4L2_BUF_FLAG_ERROR) != 0U;
    const bool incomplete = flagged_error || bytes_used == 0U;
    frames.push_back(V4l2FrameSample{
        .frame_id = next_frame_id++,
        .captured_at_steady = outcome_ts,
        .size_bytes = bytes_used,
        .outcome = incomplete ? V4l2FrameOutcome::kIncomplete : V4l2FrameOutcome::kReceived,
    });

    v4l2_buffer requeue{};
    requeue.type = buffer_type_;
    requeue.memory = V4L2_MEMORY_MMAP;
    requeue.index = dequeue.index;
    std::array<v4l2_plane, 8U> requeue_planes{};
    if (buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      requeue.m.planes = requeue_planes.data();
      requeue.length = dequeue.length;
    }
    if (IoctlRetry(fd_, VIDIOC_QBUF, &requeue) != 0) {
      error = "VIDIOC_QBUF failed while requeueing buffer: " + std::string(std::strerror(errno));
      return false;
    }
  }

  return true;
#endif
}

bool V4l2CaptureDevice::StopStreaming(std::string& error) {
  error.clear();

  if (!IsOpen()) {
    mmap_buffers_.clear();
    mmap_buffers_allocated_ = false;
    streaming_ = false;
    return true;
  }

#if !defined(__linux__)
  error = "V4L2 capture is only supported on Linux";
  return false;
#else
  std::string first_error;

  if (streaming_) {
    int stream_type = static_cast<int>(buffer_type_);
    if (IoctlRetry(fd_, VIDIOC_STREAMOFF, &stream_type) != 0) {
      first_error = "VIDIOC_STREAMOFF failed: " + std::string(std::strerror(errno));
    }
  }
  streaming_ = false;

  if (!io_ops_.munmap_fn) {
    if (first_error.empty()) {
      first_error = "V4L2 munmap operation is not configured";
    }
  } else {
    for (const MmapBuffer& buffer : mmap_buffers_) {
      if (buffer.address == nullptr || buffer.length == 0U) {
        continue;
      }
      if (io_ops_.munmap_fn(buffer.address, buffer.length) != 0 && first_error.empty()) {
        first_error = "failed to munmap V4L2 buffer: " + std::string(std::strerror(errno));
      }
    }
  }
  mmap_buffers_.clear();

  if (mmap_buffers_allocated_) {
    v4l2_requestbuffers req{};
    req.count = 0U;
    req.type = buffer_type_;
    req.memory = V4L2_MEMORY_MMAP;
    if (IoctlRetry(fd_, VIDIOC_REQBUFS, &req) != 0 && first_error.empty()) {
      first_error = "failed to release V4L2 mmap buffers: " + std::string(std::strerror(errno));
    }
  }
  mmap_buffers_allocated_ = false;

  if (!first_error.empty()) {
    error = first_error;
    return false;
  }
  return true;
#endif
}

bool V4l2CaptureDevice::IsStreaming() const {
  return streaming_;
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
