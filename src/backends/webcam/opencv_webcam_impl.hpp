#pragma once

#include "backends/camera_backend.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace labops::backends::webcam {

// Narrow property surface intentionally used by the OpenCV bootstrap path.
//
// Keeping this enum tiny avoids leaking OpenCV constants through the rest of
// the backend and lets non-OpenCV builds compile cleanly with the same API.
enum class OpenCvCaptureProperty {
  kFrameWidth = 0,
  kFrameHeight,
  kFps,
};

const char* ToString(OpenCvCaptureProperty property);

// Provider sample used by webcam-impl test mode.
//
// `stall_periods` inserts synthetic frame-period gaps before this sample so
// tests can model timeout-like cadence cliffs deterministically.
struct WebcamFrameProviderSample {
  FrameOutcome outcome = FrameOutcome::kReceived;
  std::uint32_t size_bytes = 0U;
  std::uint32_t stall_periods = 0U;
};

class IWebcamFrameProvider {
public:
  virtual ~IWebcamFrameProvider() = default;
  virtual bool Next(std::uint64_t frame_id, WebcamFrameProviderSample& sample,
                    std::string& error) = 0;
};

// Thin OpenCV wrapper used by `WebcamBackend`.
//
// Responsibilities:
// - open/close a device index with OpenCV VideoCapture
// - set/read back core stream properties
// - acquire frame samples for a time budget while classifying timeout/incomplete
//   outcomes for event + metric pipelines
// - stamp frames from monotonic capture timing while preserving the existing
//   system-clock timestamp contract for downstream artifacts
class OpenCvWebcamImpl {
public:
  OpenCvWebcamImpl();
  ~OpenCvWebcamImpl();

  OpenCvWebcamImpl(OpenCvWebcamImpl&&) noexcept;
  OpenCvWebcamImpl& operator=(OpenCvWebcamImpl&&) noexcept;

  OpenCvWebcamImpl(const OpenCvWebcamImpl&) = delete;
  OpenCvWebcamImpl& operator=(const OpenCvWebcamImpl&) = delete;

  bool OpenDevice(std::size_t device_index, std::string& error);
  bool CloseDevice(std::string& error);
  bool IsDeviceOpen() const;

  bool SetProperty(OpenCvCaptureProperty property, double value, std::string& error);
  bool GetProperty(OpenCvCaptureProperty property, double& value, std::string& error) const;

  bool SetFourcc(std::string_view fourcc_code, std::string& error);
  bool GetFourcc(std::string& fourcc_code, std::string& error) const;

  std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration,
                                      std::uint64_t& next_frame_id, std::string& error);

  // Best-effort camera index probe used by webcam discovery when fixture data
  // is not provided. Indices that fail `VideoCapture::open` are skipped.
  static std::vector<std::size_t> EnumerateDeviceIndices(std::size_t max_probe_index);

  // Enables deterministic scripted frame generation for tests.
  //
  // This mode bypasses OpenCV capture reads entirely and allows CI/local tests
  // to validate timeout/incomplete classification without any camera hardware.
  void EnableTestMode(std::unique_ptr<IWebcamFrameProvider> provider,
                      std::chrono::milliseconds frame_period,
                      std::chrono::system_clock::time_point stream_start_ts);
  void DisableTestMode();
  bool IsTestModeEnabled() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  static int ToOpenCvPropertyId(OpenCvCaptureProperty property);
};

} // namespace labops::backends::webcam
