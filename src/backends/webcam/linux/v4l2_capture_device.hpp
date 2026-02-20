#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace labops::backends::webcam {

// Capture strategy chosen for a Linux V4L2 device.
//
// `mmap_streaming` is preferred for throughput/latency. `read_fallback` exists
// for older/simpler drivers that do not expose streaming buffers.
enum class V4l2CaptureMethod {
  kMmapStreaming = 0,
  kReadFallback,
};

const char* ToString(V4l2CaptureMethod method);

// Evidence recorded after a successful native V4L2 open.
struct V4l2OpenInfo {
  std::string device_path;
  std::string driver_name;
  std::string card_name;
  std::uint32_t effective_capabilities = 0U;
  std::string capabilities_hex;
  V4l2CaptureMethod capture_method = V4l2CaptureMethod::kMmapStreaming;
  std::string capture_method_reason;
};

// Requested stream format controls for Linux native best-effort apply.
struct V4l2RequestedFormat {
  std::optional<std::uint32_t> width;
  std::optional<std::uint32_t> height;
  std::optional<std::string> pixel_format;
  std::optional<double> fps;
};

// Per-control readback evidence row emitted after best-effort apply.
struct V4l2AppliedControl {
  std::string generic_key;
  std::string node_name;
  std::string requested_value;
  std::string actual_value;
  bool supported = false;
  bool applied = false;
  bool adjusted = false;
  std::string reason;
};

struct V4l2ApplyResult {
  std::vector<V4l2AppliedControl> controls;
};

// Thin Linux V4L2 open/close helper used by webcam backend initialization.
//
// Why this exists:
// - keeps Linux-specific descriptor lifecycle in one place
// - emits explicit, actionable errors for open/querycap/capture-method failures
// - allows deterministic tests via injected IO ops (no camera required)
class V4l2CaptureDevice {
public:
  struct IoOps {
    std::function<int(const char* path, int flags)> open_fn;
    std::function<int(int fd)> close_fn;
    std::function<int(int fd, unsigned long request, void* arg)> ioctl_fn;
  };

  static IoOps DefaultIoOps();

  explicit V4l2CaptureDevice(IoOps io_ops = DefaultIoOps());
  ~V4l2CaptureDevice();

  V4l2CaptureDevice(const V4l2CaptureDevice&) = delete;
  V4l2CaptureDevice& operator=(const V4l2CaptureDevice&) = delete;

  bool Open(const std::string& device_path, V4l2OpenInfo& open_info, std::string& error);
  bool Close(std::string& error);
  bool ApplyRequestedFormatBestEffort(const V4l2RequestedFormat& request, V4l2ApplyResult& result,
                                      std::string& error);

  bool IsOpen() const;
  const std::string& DevicePath() const;
  V4l2CaptureMethod CaptureMethod() const;

  static bool ChooseCaptureMethod(std::uint32_t effective_caps, V4l2CaptureMethod& capture_method,
                                  std::string& reason);

private:
  int IoctlRetry(int fd, unsigned long request, void* arg) const;

  IoOps io_ops_;
  int fd_ = -1;
  std::string device_path_;
  std::uint32_t effective_capabilities_ = 0U;
  std::uint32_t buffer_type_ = 0U;
  V4l2CaptureMethod capture_method_ = V4l2CaptureMethod::kMmapStreaming;
};

} // namespace labops::backends::webcam
