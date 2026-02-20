#include "backends/webcam/linux/v4l2_capture_device.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>
#endif

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertTrue(const bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

void AssertContains(std::string_view text, std::string_view needle) {
  if (text.find(needle) == std::string_view::npos) {
    std::cerr << "expected text to contain '" << needle << "'\n";
    std::cerr << "actual text: " << text << '\n';
    std::abort();
  }
}

#if defined(__linux__)

std::optional<double> FpsFromTimePerFrame(const v4l2_fract& tpf) {
  if (tpf.numerator == 0U || tpf.denominator == 0U) {
    return std::nullopt;
  }
  return static_cast<double>(tpf.denominator) / static_cast<double>(tpf.numerator);
}

// Deterministic Linux V4L2 syscall mock used to test native capture logic
// without touching /dev/video* or invoking kernel ioctls.
class FakeV4l2Device {
public:
  struct DqbufStep {
    std::uint32_t bytes_used = 0U;
    std::uint32_t flags = 0U;
  };

  struct Counters {
    int open_calls = 0;
    int close_calls = 0;
    int ioctl_calls = 0;
    int poll_calls = 0;
    int streamoff_calls = 0;
    int dqbuf_calls = 0;
    int qbuf_calls = 0;
  };

  void EnableAdjustedFormat(const std::uint32_t width, const std::uint32_t height,
                            const std::uint32_t fourcc, const double fps) {
    adjust_format_ = true;
    adjust_fps_ = true;
    adjusted_width_ = width;
    adjusted_height_ = height;
    adjusted_fourcc_ = fourcc;
    adjusted_fps_ = fps;
  }

  void SetPollResults(std::vector<int> results) {
    poll_results_ = std::move(results);
    poll_cursor_ = 0U;
  }

  void SetDqbufSteps(std::vector<DqbufStep> steps) {
    dqbuf_steps_ = std::move(steps);
    dqbuf_cursor_ = 0U;
  }

  labops::backends::webcam::V4l2CaptureDevice::IoOps MakeIoOps() {
    return labops::backends::webcam::V4l2CaptureDevice::IoOps{
        .open_fn =
            [this](const char* /*path*/, const int /*flags*/) {
              ++counters.open_calls;
              return kFakeFd;
            },
        .close_fn =
            [this](const int /*fd*/) {
              ++counters.close_calls;
              return 0;
            },
        .ioctl_fn =
            [this](const int /*fd*/, const unsigned long request, void* arg) {
              ++counters.ioctl_calls;

              if (request == VIDIOC_QUERYCAP) {
                auto* cap = static_cast<v4l2_capability*>(arg);
                *cap = v4l2_capability{};
                cap->capabilities = capabilities_;
                cap->device_caps = device_capabilities_;
                std::snprintf(reinterpret_cast<char*>(cap->driver), sizeof(cap->driver), "fake_v4l2");
                std::snprintf(reinterpret_cast<char*>(cap->card), sizeof(cap->card), "Fake Camera");
                return 0;
              }

              if (request == VIDIOC_G_FMT) {
                auto* format = static_cast<v4l2_format*>(arg);
                format->fmt.pix.width = active_width_;
                format->fmt.pix.height = active_height_;
                format->fmt.pix.pixelformat = active_fourcc_;
                return 0;
              }

              if (request == VIDIOC_S_FMT) {
                auto* format = static_cast<v4l2_format*>(arg);
                if (adjust_format_) {
                  active_width_ = adjusted_width_;
                  active_height_ = adjusted_height_;
                  active_fourcc_ = adjusted_fourcc_;
                } else {
                  active_width_ = format->fmt.pix.width;
                  active_height_ = format->fmt.pix.height;
                  active_fourcc_ = format->fmt.pix.pixelformat;
                }
                format->fmt.pix.width = active_width_;
                format->fmt.pix.height = active_height_;
                format->fmt.pix.pixelformat = active_fourcc_;
                return 0;
              }

              if (request == VIDIOC_G_PARM) {
                auto* parm = static_cast<v4l2_streamparm*>(arg);
                parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
                parm->parm.capture.timeperframe.numerator = 1000U;
                parm->parm.capture.timeperframe.denominator =
                    static_cast<std::uint32_t>(active_fps_ * 1000.0);
                return 0;
              }

              if (request == VIDIOC_S_PARM) {
                auto* parm = static_cast<v4l2_streamparm*>(arg);
                const std::optional<double> requested_fps =
                    FpsFromTimePerFrame(parm->parm.capture.timeperframe);
                if (adjust_fps_) {
                  active_fps_ = adjusted_fps_;
                } else if (requested_fps.has_value()) {
                  active_fps_ = requested_fps.value();
                }
                parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
                parm->parm.capture.timeperframe.numerator = 1000U;
                parm->parm.capture.timeperframe.denominator =
                    static_cast<std::uint32_t>(active_fps_ * 1000.0);
                return 0;
              }

              if (request == VIDIOC_REQBUFS) {
                auto* req = static_cast<v4l2_requestbuffers*>(arg);
                if (req->count != 0U) {
                  req->count = 3U;
                }
                return 0;
              }

              if (request == VIDIOC_QUERYBUF) {
                auto* buffer = static_cast<v4l2_buffer*>(arg);
                buffer->length = 4096U;
                buffer->m.offset = buffer->index * 4096U;
                return 0;
              }

              if (request == VIDIOC_QBUF) {
                ++counters.qbuf_calls;
                return 0;
              }

              if (request == VIDIOC_STREAMON) {
                return 0;
              }

              if (request == VIDIOC_STREAMOFF) {
                ++counters.streamoff_calls;
                return 0;
              }

              if (request == VIDIOC_DQBUF) {
                ++counters.dqbuf_calls;
                auto* buffer = static_cast<v4l2_buffer*>(arg);
                if (dqbuf_cursor_ >= dqbuf_steps_.size()) {
                  errno = EAGAIN;
                  return -1;
                }
                const DqbufStep step = dqbuf_steps_[dqbuf_cursor_++];
                buffer->index = 0U;
                buffer->bytesused = step.bytes_used;
                buffer->flags = step.flags;
                return 0;
              }

              errno = EINVAL;
              return -1;
            },
        .poll_fn =
            [this](struct pollfd* fds, const unsigned long nfds, const int timeout_ms) {
              ++counters.poll_calls;
              if (fds == nullptr || nfds == 0U) {
                errno = EINVAL;
                return -1;
              }

              const int result = poll_cursor_ < poll_results_.size() ? poll_results_[poll_cursor_++] : 0;
              if (result == 0) {
                if (timeout_ms > 0) {
                  steady_now_ += std::chrono::milliseconds(timeout_ms);
                }
                fds[0].revents = 0;
                return 0;
              }

              steady_now_ += std::chrono::milliseconds(1);
              fds[0].revents = POLLIN;
              return 1;
            },
        .mmap_fn = [](void* /*addr*/, const std::size_t /*length*/, const int /*prot*/,
                      const int /*flags*/, const int /*fd*/, const std::int64_t /*offset*/) {
          return reinterpret_cast<void*>(0x1000000U);
        },
        .munmap_fn = [](void* /*addr*/, const std::size_t /*length*/) { return 0; },
        .steady_now_fn = [this]() { return steady_now_; },
    };
  }

  Counters counters;

private:
  static constexpr int kFakeFd = 7;

  std::uint32_t capabilities_ = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
  std::uint32_t device_capabilities_ = capabilities_;

  std::uint32_t active_width_ = 640U;
  std::uint32_t active_height_ = 480U;
  std::uint32_t active_fourcc_ = v4l2_fourcc('M', 'J', 'P', 'G');
  double active_fps_ = 30.0;

  bool adjust_format_ = false;
  bool adjust_fps_ = false;
  std::uint32_t adjusted_width_ = 1280U;
  std::uint32_t adjusted_height_ = 720U;
  std::uint32_t adjusted_fourcc_ = v4l2_fourcc('Y', 'U', 'Y', 'V');
  double adjusted_fps_ = 59.94;

  std::vector<int> poll_results_;
  std::size_t poll_cursor_ = 0U;
  std::vector<DqbufStep> dqbuf_steps_;
  std::size_t dqbuf_cursor_ = 0U;
  std::chrono::steady_clock::time_point steady_now_{};
};

const labops::backends::webcam::V4l2AppliedControl*
FindControl(const labops::backends::webcam::V4l2ApplyResult& result, std::string_view generic_key) {
  for (const auto& control : result.controls) {
    if (control.generic_key == generic_key) {
      return &control;
    }
  }
  return nullptr;
}

void TestAdjustedFormatBehavior() {
  FakeV4l2Device fake;
  fake.EnableAdjustedFormat(1280U, 720U, v4l2_fourcc('M', 'J', 'P', 'G'), 59.94);

  labops::backends::webcam::V4l2CaptureDevice device(fake.MakeIoOps());
  labops::backends::webcam::V4l2OpenInfo open_info;
  std::string error;
  AssertTrue(device.Open("/dev/video0", open_info, error), "expected fake device open to succeed");

  labops::backends::webcam::V4l2RequestedFormat request{
      .width = 1920U,
      .height = 1080U,
      .pixel_format = std::string("YUYV"),
      .fps = 60.0,
  };
  labops::backends::webcam::V4l2ApplyResult result;
  AssertTrue(device.ApplyRequestedFormatBestEffort(request, result, error),
             "expected adjusted format apply to succeed");

  const auto* width = FindControl(result, "width");
  const auto* height = FindControl(result, "height");
  const auto* pixel_format = FindControl(result, "pixel_format");
  const auto* fps = FindControl(result, "fps");
  AssertTrue(width != nullptr && height != nullptr && pixel_format != nullptr && fps != nullptr,
             "missing expected readback controls");
  AssertTrue(width->adjusted && height->adjusted && pixel_format->adjusted && fps->adjusted,
             "expected all controls to be marked adjusted");
  AssertContains(fps->reason, "adjusted");

  AssertTrue(device.Close(error), "expected close after adjusted format test");
}

void TestTimeoutSequenceClassification() {
  FakeV4l2Device fake;
  fake.SetPollResults({0, 0, 1});
  fake.SetDqbufSteps({FakeV4l2Device::DqbufStep{.bytes_used = 2048U, .flags = 0U}});

  labops::backends::webcam::V4l2CaptureDevice device(fake.MakeIoOps());
  labops::backends::webcam::V4l2OpenInfo open_info;
  std::string error;
  AssertTrue(device.Open("/dev/video1", open_info, error), "expected fake device open to succeed");

  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  AssertTrue(device.StartMmapStreaming(/*requested_buffer_count=*/3U, stream_info, error),
             "expected fake stream start to succeed");

  std::uint64_t next_frame_id = 10U;
  std::vector<labops::backends::webcam::V4l2FrameSample> frames;
  AssertTrue(device.PullFrames(std::chrono::milliseconds(401), next_frame_id, frames, error),
             "expected pull_frames timeout sequence to succeed");

  AssertTrue(frames.size() == 3U, "expected timeout, timeout, received sequence");
  AssertTrue(frames[0].outcome == labops::backends::webcam::V4l2FrameOutcome::kTimeout,
             "expected first frame timeout");
  AssertTrue(frames[1].outcome == labops::backends::webcam::V4l2FrameOutcome::kTimeout,
             "expected second frame timeout");
  AssertTrue(frames[2].outcome == labops::backends::webcam::V4l2FrameOutcome::kReceived,
             "expected third frame received");
  AssertTrue(frames[0].frame_id == 10U && frames[1].frame_id == 11U && frames[2].frame_id == 12U,
             "expected stable frame-id progression");

  AssertTrue(device.StopStreaming(error), "expected stream stop after timeout test");
  AssertTrue(device.Close(error), "expected close after timeout test");
}

void TestIncompleteBufferClassification() {
  FakeV4l2Device fake;
  fake.SetPollResults({1});
  fake.SetDqbufSteps({FakeV4l2Device::DqbufStep{.bytes_used = 0U, .flags = V4L2_BUF_FLAG_ERROR}});

  labops::backends::webcam::V4l2CaptureDevice device(fake.MakeIoOps());
  labops::backends::webcam::V4l2OpenInfo open_info;
  std::string error;
  AssertTrue(device.Open("/dev/video2", open_info, error), "expected fake device open to succeed");

  labops::backends::webcam::V4l2StreamStartInfo stream_info;
  AssertTrue(device.StartMmapStreaming(/*requested_buffer_count=*/3U, stream_info, error),
             "expected fake stream start to succeed");

  std::uint64_t next_frame_id = 50U;
  std::vector<labops::backends::webcam::V4l2FrameSample> frames;
  AssertTrue(device.PullFrames(std::chrono::milliseconds(1), next_frame_id, frames, error),
             "expected pull_frames incomplete sequence to succeed");

  AssertTrue(frames.size() == 1U, "expected a single incomplete sample");
  AssertTrue(frames[0].outcome == labops::backends::webcam::V4l2FrameOutcome::kIncomplete,
             "expected incomplete outcome");
  AssertTrue(frames[0].size_bytes == 0U, "expected zero payload bytes for incomplete sample");

  AssertTrue(device.StopStreaming(error), "expected stream stop after incomplete test");
  AssertTrue(device.Close(error), "expected close after incomplete test");
}
#endif

} // namespace

int main() {
#if !defined(__linux__)
  std::cout << "webcam_linux_mock_provider_smoke: skipped (non-linux)\n";
  return 0;
#else
  TestAdjustedFormatBehavior();
  TestTimeoutSequenceClassification();
  TestIncompleteBufferClassification();
  std::cout << "webcam_linux_mock_provider_smoke: ok\n";
  return 0;
#endif
}
