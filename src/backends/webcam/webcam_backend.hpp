#pragma once

#include "backends/camera_backend.hpp"
#include "backends/webcam/opencv_webcam_impl.hpp"
#include "backends/webcam/platform_probe.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace labops::backends::webcam {

// OpenCV bootstrap backend for direct local webcam capture.
//
// Intent:
// - keep the shared `ICameraBackend` contract intact
// - support a minimum useful webcam path (open -> configure -> pull frames)
// - preserve explicit requested-vs-actual evidence in backend config snapshots
class WebcamBackend final : public ICameraBackend {
public:
  WebcamBackend();

  bool Connect(std::string& error) override;
  bool Start(std::string& error) override;
  bool Stop(std::string& error) override;
  bool SetParam(const std::string& key, const std::string& value, std::string& error) override;
  BackendConfig DumpConfig() const override;
  std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration,
                                      std::string& error) override;

private:
  struct RequestedConfig {
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    std::optional<double> fps;
    std::optional<std::string> pixel_format;
  };

  struct UnsupportedControl {
    std::string key;
    std::string requested_value;
    std::string reason;
  };

  std::string BuildNotAvailableError() const;
  void ClearSessionConfigSnapshot();
  void RecordUnsupportedControl(std::string key, std::string requested_value, std::string reason);
  bool ResolveDeviceIndex(std::size_t& index, std::string& error) const;
  bool ApplyRequestedConfig(std::string& error);

  PlatformAvailability platform_;
  OpenCvWebcamImpl opencv_;
  BackendConfig params_;
  RequestedConfig requested_;
  std::vector<UnsupportedControl> unsupported_controls_;
  bool connected_ = false;
  bool running_ = false;
  std::uint64_t next_frame_id_ = 0;
};

} // namespace labops::backends::webcam
