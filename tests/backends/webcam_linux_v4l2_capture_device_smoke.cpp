#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
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
};

labops::backends::webcam::V4l2CaptureDevice::IoOps MakeIoOps(FakeIoState& state) {
  return labops::backends::webcam::V4l2CaptureDevice::IoOps{
      .open_fn = [&state](const char* path, const int flags) {
        ++state.open_calls;
        state.opened_path = path == nullptr ? "" : path;
        state.opened_flags = flags;
        if (state.open_result_fd < 0) {
          errno = state.errno_value;
          return -1;
        }
        return state.open_result_fd;
      },
      .close_fn = [&state](const int /*fd*/) {
        ++state.close_calls;
        if (state.close_result != 0) {
          errno = state.errno_value;
          return state.close_result;
        }
        return 0;
      },
      .ioctl_fn = [&state](const int /*fd*/, const unsigned long request, void* arg) {
        ++state.ioctl_calls;
        if (state.ioctl_result != 0) {
          errno = state.errno_value;
          return state.ioctl_result;
        }
        if (request != VIDIOC_QUERYCAP) {
          errno = EINVAL;
          return -1;
        }

        auto* capability = static_cast<v4l2_capability*>(arg);
        *capability = v4l2_capability{};
        capability->capabilities = state.caps;
        capability->device_caps = state.device_caps;
        std::snprintf(reinterpret_cast<char*>(capability->driver), sizeof(capability->driver),
                      "uvcvideo");
        std::snprintf(reinterpret_cast<char*>(capability->card), sizeof(capability->card),
                      "USB Camera");
        return 0;
      },
  };
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
  std::cout << "webcam_linux_v4l2_capture_device_smoke: ok\n";
  return 0;
#endif
}
