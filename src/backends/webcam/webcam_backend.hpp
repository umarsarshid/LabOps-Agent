#pragma once

#include "backends/camera_backend.hpp"
#include "backends/webcam/platform_probe.hpp"

#include <chrono>
#include <string>

namespace labops::backends::webcam {

// Hardware-backend placeholder for direct USB/webcam capture paths.
//
// Current milestone goal:
// - provide a compile-stable backend module across Linux/macOS/Windows
// - expose capability metadata for future per-platform implementations
// - fail fast with actionable BACKEND_NOT_AVAILABLE messaging until each
//   platform capture path is implemented
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
  std::string BuildNotAvailableError() const;

  PlatformAvailability platform_;
  BackendConfig params_;
  bool connected_ = false;
  bool running_ = false;
};

} // namespace labops::backends::webcam
