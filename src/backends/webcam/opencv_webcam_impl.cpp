#include "backends/webcam/opencv_webcam_impl.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#if LABOPS_ENABLE_WEBCAM_OPENCV
#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace labops::backends::webcam {

namespace {

constexpr std::chrono::milliseconds kReadTimeoutBudget(200);
constexpr std::chrono::milliseconds kReadFailureBackoff(5);
constexpr std::uint32_t kDefaultReceivedFrameSizeBytes = 4096U;

#if LABOPS_ENABLE_WEBCAM_OPENCV
int ToOpenCvFourcc(std::string_view code) {
  return cv::VideoWriter::fourcc(code[0], code[1], code[2], code[3]);
}

std::string DecodeOpenCvFourcc(const int value) {
  std::string decoded(4, ' ');
  decoded[0] = static_cast<char>(value & 0xFF);
  decoded[1] = static_cast<char>((value >> 8) & 0xFF);
  decoded[2] = static_cast<char>((value >> 16) & 0xFF);
  decoded[3] = static_cast<char>((value >> 24) & 0xFF);
  return decoded;
}

std::uint32_t SafeFrameSizeBytes(const cv::Mat& frame) {
  if (frame.empty()) {
    return 0U;
  }
  const auto total_bytes =
      static_cast<std::uint64_t>(frame.total()) * static_cast<std::uint64_t>(frame.elemSize());
  if (total_bytes > std::numeric_limits<std::uint32_t>::max()) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(total_bytes);
}
#endif

} // namespace

struct OpenCvWebcamImpl::Impl {
  bool test_mode_enabled = false;
  bool test_device_open = false;
  std::unique_ptr<IWebcamFrameProvider> test_provider;
  std::chrono::milliseconds test_frame_period = std::chrono::milliseconds(33);
  std::chrono::system_clock::time_point test_stream_start{};
  std::uint64_t emitted_period_cursor = 0U;
  double test_frame_width = 640.0;
  double test_frame_height = 480.0;
  double test_fps = 30.0;
  std::string test_fourcc = "MJPG";

#if LABOPS_ENABLE_WEBCAM_OPENCV
  cv::VideoCapture capture;
#endif
};

const char* ToString(const OpenCvCaptureProperty property) {
  switch (property) {
  case OpenCvCaptureProperty::kFrameWidth:
    return "frame_width";
  case OpenCvCaptureProperty::kFrameHeight:
    return "frame_height";
  case OpenCvCaptureProperty::kFps:
    return "fps";
  }
  return "unknown";
}

OpenCvWebcamImpl::OpenCvWebcamImpl() : impl_(std::make_unique<Impl>()) {}

OpenCvWebcamImpl::~OpenCvWebcamImpl() = default;

OpenCvWebcamImpl::OpenCvWebcamImpl(OpenCvWebcamImpl&&) noexcept = default;

OpenCvWebcamImpl& OpenCvWebcamImpl::operator=(OpenCvWebcamImpl&&) noexcept = default;

void OpenCvWebcamImpl::EnableTestMode(std::unique_ptr<IWebcamFrameProvider> provider,
                                      const std::chrono::milliseconds frame_period,
                                      const std::chrono::system_clock::time_point stream_start_ts) {
  impl_->test_mode_enabled = true;
  impl_->test_device_open = false;
  impl_->test_provider = std::move(provider);
  impl_->test_frame_period = frame_period > std::chrono::milliseconds::zero()
                                 ? frame_period
                                 : std::chrono::milliseconds(1);
  impl_->test_stream_start = stream_start_ts;
  impl_->emitted_period_cursor = 0U;
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (impl_->capture.isOpened()) {
    impl_->capture.release();
  }
#endif
}

void OpenCvWebcamImpl::DisableTestMode() {
  impl_->test_mode_enabled = false;
  impl_->test_device_open = false;
  impl_->test_provider.reset();
  impl_->test_frame_period = std::chrono::milliseconds(33);
  impl_->test_stream_start = std::chrono::system_clock::time_point{};
  impl_->emitted_period_cursor = 0U;
}

bool OpenCvWebcamImpl::IsTestModeEnabled() const {
  return impl_->test_mode_enabled;
}

bool OpenCvWebcamImpl::OpenDevice(const std::size_t device_index, std::string& error) {
  error.clear();
  if (impl_->test_mode_enabled) {
    (void)device_index;
    if (impl_->test_provider == nullptr) {
      error = "test mode requires a non-null frame provider";
      return false;
    }
    impl_->test_device_open = true;
    impl_->emitted_period_cursor = 0U;
    return true;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (device_index > static_cast<std::size_t>(INT_MAX)) {
    error = "BACKEND_CONNECT_FAILED: webcam index is out of range for OpenCV";
    return false;
  }
  cv::VideoCapture capture;
  if (!capture.open(static_cast<int>(device_index), cv::CAP_ANY)) {
    error = "BACKEND_CONNECT_FAILED: OpenCV could not open webcam index " +
            std::to_string(device_index);
    return false;
  }
  impl_->capture.release();
  impl_->capture = std::move(capture);
  return true;
#else
  (void)device_index;
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return false;
#endif
}

bool OpenCvWebcamImpl::CloseDevice(std::string& error) {
  error.clear();
  if (impl_->test_mode_enabled) {
    impl_->test_device_open = false;
    return true;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (!impl_->capture.isOpened()) {
    return true;
  }
  impl_->capture.release();
  return true;
#else
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return false;
#endif
}

bool OpenCvWebcamImpl::IsDeviceOpen() const {
  if (impl_->test_mode_enabled) {
    return impl_->test_device_open;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  return impl_->capture.isOpened();
#else
  return false;
#endif
}

bool OpenCvWebcamImpl::SetProperty(const OpenCvCaptureProperty property, const double value,
                                   std::string& error) {
  error.clear();
  if (impl_->test_mode_enabled) {
    if (!impl_->test_device_open) {
      error = "test webcam device must be open before setting property";
      return false;
    }
    switch (property) {
    case OpenCvCaptureProperty::kFrameWidth:
      impl_->test_frame_width = value;
      return true;
    case OpenCvCaptureProperty::kFrameHeight:
      impl_->test_frame_height = value;
      return true;
    case OpenCvCaptureProperty::kFps:
      impl_->test_fps = value;
      return true;
    }
    error = "unsupported test property";
    return false;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (!impl_->capture.isOpened()) {
    error = "webcam device must be open before setting OpenCV property";
    return false;
  }
  if (!impl_->capture.set(ToOpenCvPropertyId(property), value)) {
    error = "OpenCV rejected property set for " + std::string(ToString(property));
    return false;
  }
  return true;
#else
  (void)property;
  (void)value;
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return false;
#endif
}

bool OpenCvWebcamImpl::GetProperty(const OpenCvCaptureProperty property, double& value,
                                   std::string& error) const {
  error.clear();
  if (impl_->test_mode_enabled) {
    if (!impl_->test_device_open) {
      error = "test webcam device must be open before reading property";
      return false;
    }
    switch (property) {
    case OpenCvCaptureProperty::kFrameWidth:
      value = impl_->test_frame_width;
      return true;
    case OpenCvCaptureProperty::kFrameHeight:
      value = impl_->test_frame_height;
      return true;
    case OpenCvCaptureProperty::kFps:
      value = impl_->test_fps;
      return true;
    }
    error = "unsupported test property";
    return false;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (!impl_->capture.isOpened()) {
    error = "webcam device must be open before reading OpenCV property";
    return false;
  }
  const double read_value = impl_->capture.get(ToOpenCvPropertyId(property));
  if (!std::isfinite(read_value) || read_value <= 0.0) {
    error = "OpenCV returned an unreadable value for property " + std::string(ToString(property));
    return false;
  }
  value = read_value;
  return true;
#else
  (void)property;
  (void)value;
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return false;
#endif
}

bool OpenCvWebcamImpl::SetFourcc(std::string_view fourcc_code, std::string& error) {
  error.clear();
  if (fourcc_code.size() != 4U) {
    error = "pixel format must be exactly 4 characters for OpenCV fourcc";
    return false;
  }
  if (impl_->test_mode_enabled) {
    if (!impl_->test_device_open) {
      error = "test webcam device must be open before setting fourcc";
      return false;
    }
    impl_->test_fourcc = std::string(fourcc_code);
    return true;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (!impl_->capture.isOpened()) {
    error = "webcam device must be open before setting OpenCV fourcc";
    return false;
  }
  if (!impl_->capture.set(cv::CAP_PROP_FOURCC, static_cast<double>(ToOpenCvFourcc(fourcc_code)))) {
    error = "OpenCV rejected pixel format request '" + std::string(fourcc_code) + "'";
    return false;
  }
  return true;
#else
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return false;
#endif
}

bool OpenCvWebcamImpl::GetFourcc(std::string& fourcc_code, std::string& error) const {
  error.clear();
  if (impl_->test_mode_enabled) {
    if (!impl_->test_device_open) {
      error = "test webcam device must be open before reading fourcc";
      return false;
    }
    fourcc_code = impl_->test_fourcc;
    return true;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (!impl_->capture.isOpened()) {
    error = "webcam device must be open before reading OpenCV fourcc";
    return false;
  }
  const double raw_value = impl_->capture.get(cv::CAP_PROP_FOURCC);
  if (!std::isfinite(raw_value) || raw_value <= 0.0) {
    error = "OpenCV could not read back a valid fourcc value";
    return false;
  }
  fourcc_code = DecodeOpenCvFourcc(static_cast<int>(raw_value));
  return true;
#else
  (void)fourcc_code;
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return false;
#endif
}

std::vector<FrameSample> OpenCvWebcamImpl::PullFrames(std::chrono::milliseconds duration,
                                                      std::uint64_t& next_frame_id,
                                                      std::string& error) {
  error.clear();
  if (impl_->test_mode_enabled) {
    if (duration < std::chrono::milliseconds::zero()) {
      error = "pull_frames duration cannot be negative";
      return {};
    }
    if (duration == std::chrono::milliseconds::zero()) {
      return {};
    }
    if (!impl_->test_device_open) {
      error = "test webcam device must be open before pull_frames";
      return {};
    }
    if (impl_->test_provider == nullptr) {
      error = "test mode frame provider is not configured";
      return {};
    }

    const auto frame_period_ms = std::max<std::int64_t>(1, impl_->test_frame_period.count());
    const std::uint64_t frame_count =
        static_cast<std::uint64_t>(duration.count() / frame_period_ms);
    if (frame_count == 0U) {
      return {};
    }

    std::vector<FrameSample> frames;
    frames.reserve(static_cast<std::size_t>(frame_count));
    for (std::uint64_t i = 0; i < frame_count; ++i) {
      WebcamFrameProviderSample scripted;
      if (!impl_->test_provider->Next(next_frame_id, scripted, error)) {
        return {};
      }

      impl_->emitted_period_cursor += scripted.stall_periods;
      FrameSample frame;
      frame.frame_id = next_frame_id++;
      frame.timestamp =
          impl_->test_stream_start +
          impl_->test_frame_period * static_cast<std::int64_t>(impl_->emitted_period_cursor);
      ++impl_->emitted_period_cursor;

      switch (scripted.outcome) {
      case FrameOutcome::kTimeout:
        frame.size_bytes = 0U;
        frame.dropped = true;
        frame.outcome = FrameOutcome::kTimeout;
        break;
      case FrameOutcome::kIncomplete:
        frame.size_bytes = scripted.size_bytes == 0U ? 1U : scripted.size_bytes;
        frame.dropped = true;
        frame.outcome = FrameOutcome::kIncomplete;
        break;
      case FrameOutcome::kDropped:
        frame.size_bytes = scripted.size_bytes;
        frame.dropped = true;
        frame.outcome = FrameOutcome::kDropped;
        break;
      case FrameOutcome::kReceived:
      default:
        frame.size_bytes =
            scripted.size_bytes == 0U ? kDefaultReceivedFrameSizeBytes : scripted.size_bytes;
        frame.dropped = false;
        frame.outcome = FrameOutcome::kReceived;
        break;
      }

      frames.push_back(frame);
    }
    return frames;
  }
#if LABOPS_ENABLE_WEBCAM_OPENCV
  if (duration < std::chrono::milliseconds::zero()) {
    error = "pull_frames duration cannot be negative";
    return {};
  }
  if (duration == std::chrono::milliseconds::zero()) {
    return {};
  }
  if (!impl_->capture.isOpened()) {
    error = "webcam device must be open before pull_frames";
    return {};
  }

  std::vector<FrameSample> frames;
  const auto deadline = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto read_started_at = std::chrono::steady_clock::now();
    cv::Mat frame;
    const bool read_ok = impl_->capture.read(frame);
    const auto read_finished_at = std::chrono::steady_clock::now();

    FrameSample sample;
    sample.frame_id = next_frame_id++;
    sample.timestamp = std::chrono::system_clock::now();

    if (!read_ok) {
      sample.size_bytes = 0;
      sample.dropped = true;
      sample.outcome = (read_finished_at - read_started_at) >= kReadTimeoutBudget
                           ? FrameOutcome::kTimeout
                           : FrameOutcome::kIncomplete;
      frames.push_back(sample);
      std::this_thread::sleep_for(kReadFailureBackoff);
      continue;
    }

    if (frame.empty()) {
      sample.size_bytes = 0;
      sample.dropped = true;
      sample.outcome = FrameOutcome::kIncomplete;
      frames.push_back(sample);
      std::this_thread::sleep_for(kReadFailureBackoff);
      continue;
    }

    sample.size_bytes = SafeFrameSizeBytes(frame);
    sample.dropped = false;
    sample.outcome = FrameOutcome::kReceived;
    frames.push_back(sample);
  }

  return frames;
#else
  (void)duration;
  (void)next_frame_id;
  error = "BACKEND_NOT_AVAILABLE: OpenCV webcam bootstrap is not compiled in this build";
  return {};
#endif
}

std::vector<std::size_t>
OpenCvWebcamImpl::EnumerateDeviceIndices(const std::size_t max_probe_index) {
  std::vector<std::size_t> device_indices;
#if LABOPS_ENABLE_WEBCAM_OPENCV
  for (std::size_t index = 0; index <= max_probe_index; ++index) {
    cv::VideoCapture capture;
    if (!capture.open(static_cast<int>(index), cv::CAP_ANY)) {
      continue;
    }
    device_indices.push_back(index);
    capture.release();
  }
#else
  (void)max_probe_index;
#endif
  return device_indices;
}

int OpenCvWebcamImpl::ToOpenCvPropertyId(const OpenCvCaptureProperty property) {
#if LABOPS_ENABLE_WEBCAM_OPENCV
  switch (property) {
  case OpenCvCaptureProperty::kFrameWidth:
    return cv::CAP_PROP_FRAME_WIDTH;
  case OpenCvCaptureProperty::kFrameHeight:
    return cv::CAP_PROP_FRAME_HEIGHT;
  case OpenCvCaptureProperty::kFps:
    return cv::CAP_PROP_FPS;
  }
  return cv::CAP_PROP_FRAME_WIDTH;
#else
  (void)property;
  return 0;
#endif
}

} // namespace labops::backends::webcam
