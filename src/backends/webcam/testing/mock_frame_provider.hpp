#pragma once

#include "backends/webcam/opencv_webcam_impl.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace labops::backends::webcam::testing {

// Scripted provider used by webcam-impl tests to avoid OpenCV/hardware
// dependencies while still exercising frame-outcome classification behavior.
class MockFrameProvider final : public IWebcamFrameProvider {
public:
  explicit MockFrameProvider(std::vector<WebcamFrameProviderSample> script);

  bool Next(std::uint64_t frame_id, WebcamFrameProviderSample& sample, std::string& error) override;

  std::size_t next_index() const;
  std::size_t script_size() const;

private:
  std::vector<WebcamFrameProviderSample> script_;
  std::size_t next_index_ = 0U;
};

} // namespace labops::backends::webcam::testing
