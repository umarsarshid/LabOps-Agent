#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>

#if defined(__linux__)
#include <cstdio>
#include <linux/videodev2.h>
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
  int open_calls = 0;
  int close_calls = 0;
  int ioctl_calls = 0;

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
  TestApplyRequestedFormatCapturesAdjustedReadback();
  TestApplyRequestedFormatCapturesUnsupported();
  std::cout << "webcam_linux_v4l2_capture_device_smoke: ok\n";
  return 0;
#endif
}
