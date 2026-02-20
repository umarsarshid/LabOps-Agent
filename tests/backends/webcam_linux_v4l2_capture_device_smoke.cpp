#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <cstdio>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>
#endif

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected text to contain '" << needle << "'\n";
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

void AssertTrue(const bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

#if defined(__linux__)
struct FakeIoState {
  struct DqbufResult {
    std::uint32_t bytes_used = 0U;
    std::uint32_t flags = 0U;
  };

  int open_calls = 0;
  int close_calls = 0;
  int ioctl_calls = 0;
  int poll_calls = 0;
  int reqbuf_calls = 0;
  int querybuf_calls = 0;
  int dqbuf_calls = 0;
  int qbuf_calls = 0;
  int streamon_calls = 0;
  int streamoff_calls = 0;
  int mmap_calls = 0;
  int munmap_calls = 0;

  int open_result_fd = 17;
  int close_result = 0;
  int ioctl_result = 0;
  int errno_value = 0;

  std::string opened_path;
  int opened_flags = 0;

  std::uint32_t caps = 0U;
  std::uint32_t device_caps = 0U;

  // Active negotiated stream settings used by G_FMT/G_PARM reads.
  std::uint32_t active_width = 640U;
  std::uint32_t active_height = 480U;
  std::uint32_t active_fourcc = v4l2_fourcc('M', 'J', 'P', 'G');
  double active_fps = 30.0;

  // Error/behavior toggles for format/parm ioctls.
  bool fail_g_fmt = false;
  bool fail_s_fmt = false;
  bool fail_g_parm = false;
  bool fail_s_parm = false;
  bool supports_timeperframe = true;
  bool adjust_format = false;
  bool adjust_fps = false;

  std::uint32_t adjusted_width = 1280U;
  std::uint32_t adjusted_height = 720U;
  std::uint32_t adjusted_fourcc = v4l2_fourcc('Y', 'U', 'Y', 'V');
  double adjusted_fps = 59.94;

  bool fail_reqbuf = false;
  bool fail_querybuf = false;
  bool fail_qbuf = false;
  bool fail_streamon = false;
  bool fail_streamoff = false;
  bool fail_mmap = false;
  bool fail_munmap = false;
  bool fail_dqbuf = false;
  std::uint32_t reqbuf_count_return = 4U;

  std::vector<int> poll_results;
  std::size_t poll_cursor = 0U;
  std::vector<DqbufResult> dqbuf_results;
  std::size_t dqbuf_cursor = 0U;
  std::chrono::steady_clock::time_point steady_now = std::chrono::steady_clock::time_point{};
};

std::optional<double> FpsFromTimePerFrame(const v4l2_fract& tpf) {
  if (tpf.numerator == 0U || tpf.denominator == 0U) {
    return std::nullopt;
  }
  return static_cast<double>(tpf.denominator) / static_cast<double>(tpf.numerator);
}

labops::backends::webcam::V4l2CaptureDevice::IoOps MakeIoOps(FakeIoState& state) {
  return labops::backends::webcam::V4l2CaptureDevice::IoOps{
      .open_fn =
          [&state](const char* path, const int flags) {
            ++state.open_calls;
            state.opened_path = path == nullptr ? "" : path;
            state.opened_flags = flags;
            if (state.open_result_fd < 0) {
              errno = state.errno_value;
              return -1;
            }
            return state.open_result_fd;
          },
      .close_fn =
          [&state](const int /*fd*/) {
            ++state.close_calls;
            if (state.close_result != 0) {
              errno = state.errno_value;
              return state.close_result;
            }
            return 0;
          },
      .ioctl_fn =
          [&state](const int /*fd*/, const unsigned long request, void* arg) {
            ++state.ioctl_calls;
            if (state.ioctl_result != 0) {
              errno = state.errno_value;
              return state.ioctl_result;
            }
            if (request == VIDIOC_QUERYCAP) {
              auto* capability = static_cast<v4l2_capability*>(arg);
              *capability = v4l2_capability{};
              capability->capabilities = state.caps;
              capability->device_caps = state.device_caps;
              std::snprintf(reinterpret_cast<char*>(capability->driver), sizeof(capability->driver),
                            "uvcvideo");
              std::snprintf(reinterpret_cast<char*>(capability->card), sizeof(capability->card),
                            "USB Camera");
              return 0;
            }

            if (request == VIDIOC_G_FMT) {
              if (state.fail_g_fmt) {
                errno = EINVAL;
                return -1;
              }
              auto* format = static_cast<v4l2_format*>(arg);
              if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                format->fmt.pix.width = state.active_width;
                format->fmt.pix.height = state.active_height;
                format->fmt.pix.pixelformat = state.active_fourcc;
              } else {
                format->fmt.pix_mp.width = state.active_width;
                format->fmt.pix_mp.height = state.active_height;
                format->fmt.pix_mp.pixelformat = state.active_fourcc;
              }
              return 0;
            }

            if (request == VIDIOC_S_FMT) {
              if (state.fail_s_fmt) {
                errno = EINVAL;
                return -1;
              }
              auto* format = static_cast<v4l2_format*>(arg);
              if (state.adjust_format) {
                state.active_width = state.adjusted_width;
                state.active_height = state.adjusted_height;
                state.active_fourcc = state.adjusted_fourcc;
              } else if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                state.active_width = format->fmt.pix.width;
                state.active_height = format->fmt.pix.height;
                state.active_fourcc = format->fmt.pix.pixelformat;
              } else {
                state.active_width = format->fmt.pix_mp.width;
                state.active_height = format->fmt.pix_mp.height;
                state.active_fourcc = format->fmt.pix_mp.pixelformat;
              }

              if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                format->fmt.pix.width = state.active_width;
                format->fmt.pix.height = state.active_height;
                format->fmt.pix.pixelformat = state.active_fourcc;
              } else {
                format->fmt.pix_mp.width = state.active_width;
                format->fmt.pix_mp.height = state.active_height;
                format->fmt.pix_mp.pixelformat = state.active_fourcc;
              }
              return 0;
            }

            if (request == VIDIOC_REQBUFS) {
              ++state.reqbuf_calls;
              auto* req = static_cast<v4l2_requestbuffers*>(arg);
              if (state.fail_reqbuf) {
                errno = EINVAL;
                return -1;
              }
              if (req->memory != V4L2_MEMORY_MMAP) {
                errno = EINVAL;
                return -1;
              }
              if (req->count == 0U) {
                return 0;
              }
              req->count = state.reqbuf_count_return;
              return 0;
            }

            if (request == VIDIOC_QUERYBUF) {
              ++state.querybuf_calls;
              if (state.fail_querybuf) {
                errno = EINVAL;
                return -1;
              }
              auto* buffer = static_cast<v4l2_buffer*>(arg);
              if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                buffer->length = 4096U;
                buffer->m.offset = buffer->index * 4096U;
              } else {
                if (buffer->m.planes == nullptr || buffer->length == 0U) {
                  errno = EINVAL;
                  return -1;
                }
                buffer->m.planes[0].length = 4096U;
                buffer->m.planes[0].m.mem_offset = buffer->index * 4096U;
                buffer->length = 1U;
              }
              return 0;
            }

            if (request == VIDIOC_DQBUF) {
              ++state.dqbuf_calls;
              if (state.fail_dqbuf) {
                errno = EIO;
                return -1;
              }
              if (state.dqbuf_cursor >= state.dqbuf_results.size()) {
                errno = EAGAIN;
                return -1;
              }

              auto* buffer = static_cast<v4l2_buffer*>(arg);
              const FakeIoState::DqbufResult result = state.dqbuf_results[state.dqbuf_cursor++];
              buffer->index = 0U;
              buffer->flags = result.flags;
              if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                buffer->bytesused = result.bytes_used;
              } else {
                if (buffer->m.planes == nullptr || buffer->length == 0U) {
                  errno = EINVAL;
                  return -1;
                }
                buffer->m.planes[0].bytesused = result.bytes_used;
              }
              return 0;
            }

            if (request == VIDIOC_QBUF) {
              ++state.qbuf_calls;
              if (state.fail_qbuf) {
                errno = EIO;
                return -1;
              }
              return 0;
            }

            if (request == VIDIOC_STREAMON) {
              ++state.streamon_calls;
              if (state.fail_streamon) {
                errno = EBUSY;
                return -1;
              }
              return 0;
            }

            if (request == VIDIOC_STREAMOFF) {
              ++state.streamoff_calls;
              if (state.fail_streamoff) {
                errno = EIO;
                return -1;
              }
              return 0;
            }

            if (request == VIDIOC_G_PARM) {
              if (state.fail_g_parm) {
                errno = EINVAL;
                return -1;
              }
              auto* parm = static_cast<v4l2_streamparm*>(arg);
              parm->parm.capture.capability =
                  state.supports_timeperframe ? V4L2_CAP_TIMEPERFRAME : 0U;
              if (state.active_fps > 0.0) {
                parm->parm.capture.timeperframe.numerator = 1000U;
                parm->parm.capture.timeperframe.denominator =
                    static_cast<std::uint32_t>(state.active_fps * 1000.0);
              }
              return 0;
            }

            if (request == VIDIOC_S_PARM) {
              if (state.fail_s_parm) {
                errno = EINVAL;
                return -1;
              }
              auto* parm = static_cast<v4l2_streamparm*>(arg);
              if (!state.supports_timeperframe) {
                parm->parm.capture.capability = 0U;
                return 0;
              }

              const std::optional<double> requested_fps =
                  FpsFromTimePerFrame(parm->parm.capture.timeperframe);
              if (state.adjust_fps) {
                state.active_fps = state.adjusted_fps;
              } else if (requested_fps.has_value()) {
                state.active_fps = requested_fps.value();
              }
              parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
              parm->parm.capture.timeperframe.numerator = 1000U;
              parm->parm.capture.timeperframe.denominator =
                  static_cast<std::uint32_t>(state.active_fps * 1000.0);
              return 0;
            }

            errno = EINVAL;
            return -1;
          },
      .poll_fn =
          [&state](struct pollfd* fds, const unsigned long nfds, const int timeout_ms) {
            ++state.poll_calls;
            if (nfds == 0U || fds == nullptr) {
              errno = EINVAL;
              return -1;
            }

            const int result = state.poll_cursor < state.poll_results.size()
                                   ? state.poll_results[state.poll_cursor++]
                                   : 0;
            if (result < 0) {
              errno = EINTR;
              return -1;
            }

            if (result == 0) {
              if (timeout_ms > 0) {
                state.steady_now += std::chrono::milliseconds(timeout_ms);
              }
              fds[0].revents = 0;
              return 0;
            }

            state.steady_now += std::chrono::milliseconds(1);
            fds[0].revents = POLLIN;
            return 1;
          },
      .mmap_fn = [&state](void* /*addr*/, const std::size_t /*length*/, const int /*prot*/,
                          const int /*flags*/, const int /*fd*/,
                          const std::int64_t /*offset*/) -> void* {
        ++state.mmap_calls;
        if (state.fail_mmap) {
          errno = ENOMEM;
          return MAP_FAILED;
        }
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000000U) +
                                       static_cast<std::uintptr_t>(state.mmap_calls * 0x1000));
      },
      .munmap_fn =
          [&state](void* /*addr*/, const std::size_t /*length*/) {
            ++state.munmap_calls;
            if (state.fail_munmap) {
              errno = EIO;
              return -1;
            }
            return 0;
          },
      .steady_now_fn = [&state]() { return state.steady_now; },
  };
}

const labops::backends::webcam::V4l2AppliedControl*
FindControl(const labops::backends::webcam::V4l2ApplyResult& result, std::string_view generic_key) {
  for (const auto& control : result.controls) {
    if (control.generic_key == generic_key) {
      return &control;
    }
  }
  return nullptr;
}

void TestOpenPrefersMmapWhenAvailable() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video7", info, error)) {
    Fail("expected mmap-capable device to open");
  }

  AssertTrue(info.capture_method == labops::backends::webcam::V4l2CaptureMethod::kMmapStreaming,
             "expected mmap capture method");
  AssertContains(info.capture_method_reason, "preferred");
  AssertContains(info.capabilities_hex, "0x");
  AssertTrue(device.IsOpen(), "device should be open after successful open");

  if (!device.Close(error)) {
    Fail("expected first close to succeed");
  }
  if (!device.Close(error)) {
    Fail("expected idempotent close to succeed");
  }
}

void TestOpenFallsBackToReadWhenStreamingMissing() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video3", info, error)) {
    Fail("expected read-fallback capable device to open");
  }
  AssertTrue(info.capture_method == labops::backends::webcam::V4l2CaptureMethod::kReadFallback,
             "expected read fallback method");
  AssertContains(info.capture_method_reason, "read()");
  if (!device.Close(error)) {
    Fail("expected close to succeed for read-fallback device");
  }
}

void TestOpenFailsWithActionableErrors() {
  {
    FakeIoState state;
    state.open_result_fd = -1;
    state.errno_value = ENOENT;
    labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

    labops::backends::webcam::V4l2OpenInfo info;
    std::string error;
    if (device.Open("/dev/video404", info, error)) {
      Fail("expected open failure to fail");
    }
    AssertContains(error, "failed to open V4L2 device");
  }

  {
    FakeIoState state;
    state.ioctl_result = -1;
    state.errno_value = ENOTTY;
    state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

    labops::backends::webcam::V4l2OpenInfo info;
    std::string error;
    if (device.Open("/dev/video9", info, error)) {
      Fail("expected querycap failure to fail");
    }
    AssertContains(error, "VIDIOC_QUERYCAP failed");
    AssertTrue(state.close_calls == 1, "expected fd cleanup on querycap failure");
  }

  {
    FakeIoState state;
    state.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

    labops::backends::webcam::V4l2OpenInfo info;
    std::string error;
    if (device.Open("/dev/video1", info, error)) {
      Fail("expected no-capture-capability open to fail");
    }
    AssertContains(error, "does not support video capture");
  }

  {
    FakeIoState state;
    state.device_caps = V4L2_CAP_VIDEO_CAPTURE;
    labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

    labops::backends::webcam::V4l2OpenInfo info;
    std::string error;
    if (device.Open("/dev/video2", info, error)) {
      Fail("expected no-method-capability open to fail");
    }
    AssertContains(error, "does not support mmap streaming or read() capture");
  }
}

void TestCloseFailureIsActionable() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
  state.close_result = -1;
  state.errno_value = EIO;
  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video12", info, error)) {
    Fail("expected open to succeed before close failure test");
  }
  if (device.Close(error)) {
    Fail("expected close to fail when close op returns non-zero");
  }
  AssertContains(error, "failed to close V4L2 device");

  state.close_result = 0;
  if (!device.Close(error)) {
    Fail("expected close retry to succeed after restoring close op");
  }
}

void TestCloseStillClosesFdWhenStreamStopFails() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
  state.fail_streamoff = true;
  state.errno_value = EIO;
  state.reqbuf_count_return = 2U;
  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video17", info, error)) {
    Fail("expected open to succeed before stop-failure close test");
  }
  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  if (!device.StartMmapStreaming(/*requested_buffer_count=*/2U, stream_info, error)) {
    Fail("expected stream start before stop-failure close test");
  }

  if (device.Close(error)) {
    Fail("expected close to report stream teardown failure");
  }
  AssertContains(error, "stream teardown reported an error");
  AssertTrue(state.close_calls == 1, "expected close fd call even when streamoff failed");
  AssertTrue(!device.IsOpen(), "device fd should be closed after close teardown attempt");

  if (!device.Close(error)) {
    Fail("expected idempotent close to succeed after fd was released");
  }
}

void TestApplyRequestedFormatCapturesAdjustedReadback() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
  state.adjust_format = true;
  state.adjusted_width = 1280;
  state.adjusted_height = 720;
  state.adjusted_fourcc = v4l2_fourcc('M', 'J', 'P', 'G');
  state.adjust_fps = true;
  state.adjusted_fps = 59.94;

  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));
  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video6", info, error)) {
    Fail("expected open to succeed before apply test");
  }

  labops::backends::webcam::V4l2RequestedFormat request{
      .width = 1920U,
      .height = 1080U,
      .pixel_format = std::string("YUYV"),
      .fps = 60.0,
  };
  labops::backends::webcam::V4l2ApplyResult result;
  if (!device.ApplyRequestedFormatBestEffort(request, result, error)) {
    Fail("expected best-effort format apply to succeed");
  }
  AssertTrue(result.controls.size() == 4U, "expected 4 readback control rows");

  const auto* width = FindControl(result, "width");
  const auto* height = FindControl(result, "height");
  const auto* pixel_format = FindControl(result, "pixel_format");
  const auto* fps = FindControl(result, "fps");
  if (width == nullptr || height == nullptr || pixel_format == nullptr || fps == nullptr) {
    Fail("missing expected readback control rows");
  }

  AssertTrue(width->supported && width->applied && width->adjusted,
             "width should be marked adjusted");
  AssertTrue(height->supported && height->applied && height->adjusted,
             "height should be marked adjusted");
  AssertTrue(pixel_format->supported && pixel_format->applied && pixel_format->adjusted,
             "pixel format should be marked adjusted");
  AssertTrue(fps->supported && fps->applied && fps->adjusted, "fps should be marked adjusted");
  AssertContains(fps->reason, "adjusted");

  if (!device.Close(error)) {
    Fail("expected close to succeed");
  }
}

void TestApplyRequestedFormatCapturesUnsupported() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
  state.fail_s_fmt = true;
  state.supports_timeperframe = false;

  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));
  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video8", info, error)) {
    Fail("expected open to succeed before unsupported apply test");
  }

  labops::backends::webcam::V4l2RequestedFormat request{
      .width = 1920U,
      .height = 1080U,
      .pixel_format = std::string("YUYV"),
      .fps = 30.0,
  };
  labops::backends::webcam::V4l2ApplyResult result;
  if (!device.ApplyRequestedFormatBestEffort(request, result, error)) {
    Fail("best-effort apply should not hard-fail unsupported controls");
  }

  const auto* width = FindControl(result, "width");
  const auto* height = FindControl(result, "height");
  const auto* pixel_format = FindControl(result, "pixel_format");
  const auto* fps = FindControl(result, "fps");
  if (width == nullptr || height == nullptr || pixel_format == nullptr || fps == nullptr) {
    Fail("missing expected unsupported control rows");
  }
  AssertTrue(!width->supported && !width->applied, "width should be unsupported");
  AssertTrue(!height->supported && !height->applied, "height should be unsupported");
  AssertTrue(!pixel_format->supported && !pixel_format->applied,
             "pixel format should be unsupported");
  AssertTrue(!fps->supported && !fps->applied, "fps should be unsupported");
  AssertContains(width->reason, "VIDIOC_S_FMT");
  AssertContains(fps->reason, "TIMEPERFRAME");

  if (!device.Close(error)) {
    Fail("expected close to succeed");
  }
}

void TestMmapStreamingStartStop() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
  state.reqbuf_count_return = 3U;

  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));
  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video11", info, error)) {
    Fail("expected open to succeed before mmap streaming test");
  }

  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  if (!device.StartMmapStreaming(/*requested_buffer_count=*/3U, stream_info, error)) {
    Fail("expected mmap streaming to start");
  }
  AssertTrue(device.IsStreaming(), "expected stream to be marked running");
  AssertTrue(stream_info.buffer_count == 3U, "expected stream buffer count from reqbuf");
  AssertTrue(state.reqbuf_calls >= 1, "expected VIDIOC_REQBUFS calls");
  AssertTrue(state.querybuf_calls == 3, "expected one VIDIOC_QUERYBUF per buffer");
  AssertTrue(state.qbuf_calls == 3, "expected one VIDIOC_QBUF per buffer");
  AssertTrue(state.mmap_calls == 3, "expected one mmap per buffer");
  AssertTrue(state.streamon_calls == 1, "expected one VIDIOC_STREAMON");

  if (!device.StopStreaming(error)) {
    Fail("expected stream stop to succeed");
  }
  AssertTrue(!device.IsStreaming(), "expected stream stopped flag");
  AssertTrue(state.streamoff_calls == 1, "expected one VIDIOC_STREAMOFF");
  AssertTrue(state.munmap_calls == 3, "expected one munmap per buffer");
  AssertTrue(state.reqbuf_calls >= 2, "expected reqbuf release call");

  if (!device.Close(error)) {
    Fail("expected close to succeed after stream stop");
  }
}

void TestMmapStreamingFailureIsActionable() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
  state.fail_reqbuf = true;
  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video13", info, error)) {
    Fail("expected open to succeed before reqbuf failure test");
  }

  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  if (device.StartMmapStreaming(/*requested_buffer_count=*/4U, stream_info, error)) {
    Fail("expected mmap streaming start to fail when REQBUFS fails");
  }
  AssertContains(error, "VIDIOC_REQBUFS failed");
  AssertTrue(!device.IsStreaming(), "stream should remain stopped after start failure");

  state.fail_reqbuf = false;
  if (!device.Close(error)) {
    Fail("expected close to succeed after failed start");
  }
}

void TestMmapStreamingRejectsReadFallbackDevices() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));

  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video14", info, error)) {
    Fail("expected read-fallback open to succeed");
  }
  AssertTrue(info.capture_method == labops::backends::webcam::V4l2CaptureMethod::kReadFallback,
             "expected read fallback mode");

  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  if (device.StartMmapStreaming(/*requested_buffer_count=*/2U, stream_info, error)) {
    Fail("expected mmap streaming start to fail on read-fallback device");
  }
  AssertContains(error, "mmap streaming is unavailable");

  if (!device.Close(error)) {
    Fail("expected close to succeed after read-fallback start rejection");
  }
}

void TestPullFramesClassifiesTimeoutReceivedIncomplete() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
  state.reqbuf_count_return = 2U;
  state.poll_results = {0, 1, 1};
  state.dqbuf_results = {
      FakeIoState::DqbufResult{.bytes_used = 2048U, .flags = 0U},
      FakeIoState::DqbufResult{.bytes_used = 0U, .flags = V4L2_BUF_FLAG_ERROR},
  };

  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));
  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video15", info, error)) {
    Fail("expected open to succeed before pull_frames test");
  }
  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  if (!device.StartMmapStreaming(/*requested_buffer_count=*/2U, stream_info, error)) {
    Fail("expected streaming start before pull_frames test");
  }

  std::uint64_t next_frame_id = 100U;
  std::vector<labops::backends::webcam::V4l2FrameSample> frames;
  if (!device.PullFrames(std::chrono::milliseconds(202), next_frame_id, frames, error)) {
    Fail("expected pull_frames to succeed");
  }
  AssertTrue(frames.size() == 3U, "expected timeout + received + incomplete samples");
  AssertTrue(frames[0].frame_id == 100U && frames[1].frame_id == 101U && frames[2].frame_id == 102U,
             "expected sequential frame ids");
  AssertTrue(frames[0].outcome == labops::backends::webcam::V4l2FrameOutcome::kTimeout,
             "expected first sample timeout");
  AssertTrue(frames[1].outcome == labops::backends::webcam::V4l2FrameOutcome::kReceived,
             "expected second sample received");
  AssertTrue(frames[2].outcome == labops::backends::webcam::V4l2FrameOutcome::kIncomplete,
             "expected third sample incomplete");
  AssertTrue(frames[1].size_bytes == 2048U, "expected received bytes from dequeue");
  AssertTrue(frames[2].size_bytes == 0U, "expected incomplete bytes from dequeue");
  AssertTrue(next_frame_id == 103U, "expected next_frame_id advanced by emitted samples");
  AssertTrue(frames[0].captured_at_steady <= frames[1].captured_at_steady &&
                 frames[1].captured_at_steady <= frames[2].captured_at_steady,
             "expected monotonic steady timestamps");
  AssertTrue(state.dqbuf_calls == 2, "expected two dequeues for ready polls");
  AssertTrue(state.qbuf_calls == 4, "expected initial queue + requeue calls");

  if (!device.StopStreaming(error)) {
    Fail("expected stop streaming after pull_frames test");
  }
  if (!device.Close(error)) {
    Fail("expected close after pull_frames test");
  }
}

void TestPullFramesDqbufFailureIsActionable() {
  FakeIoState state;
  state.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
  state.reqbuf_count_return = 1U;
  state.poll_results = {1};
  state.fail_dqbuf = true;

  labops::backends::webcam::V4l2CaptureDevice device(MakeIoOps(state));
  labops::backends::webcam::V4l2OpenInfo info;
  std::string error;
  if (!device.Open("/dev/video16", info, error)) {
    Fail("expected open to succeed before dqbuf failure test");
  }
  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  if (!device.StartMmapStreaming(/*requested_buffer_count=*/1U, stream_info, error)) {
    Fail("expected streaming start before dqbuf failure test");
  }

  std::uint64_t next_frame_id = 1U;
  std::vector<labops::backends::webcam::V4l2FrameSample> frames;
  if (device.PullFrames(std::chrono::milliseconds(10), next_frame_id, frames, error)) {
    Fail("expected pull_frames to fail when DQBUF fails");
  }
  AssertContains(error, "VIDIOC_DQBUF failed");

  // Restore fake state so cleanup can proceed.
  state.fail_dqbuf = false;
  if (!device.StopStreaming(error)) {
    Fail("expected stop streaming after dqbuf failure");
  }
  if (!device.Close(error)) {
    Fail("expected close after dqbuf failure");
  }
}
#endif

} // namespace

int main() {
#if !defined(__linux__)
  std::cout << "webcam_linux_v4l2_capture_device_smoke: skipped (non-linux)\n";
  return 0;
#else
  TestOpenPrefersMmapWhenAvailable();
  TestOpenFallsBackToReadWhenStreamingMissing();
  TestOpenFailsWithActionableErrors();
  TestCloseFailureIsActionable();
  TestCloseStillClosesFdWhenStreamStopFails();
  TestApplyRequestedFormatCapturesAdjustedReadback();
  TestApplyRequestedFormatCapturesUnsupported();
  TestMmapStreamingStartStop();
  TestMmapStreamingFailureIsActionable();
  TestMmapStreamingRejectsReadFallbackDevices();
  TestPullFramesClassifiesTimeoutReceivedIncomplete();
  TestPullFramesDqbufFailureIsActionable();
  std::cout << "webcam_linux_v4l2_capture_device_smoke: ok\n";
  return 0;
#endif
}
