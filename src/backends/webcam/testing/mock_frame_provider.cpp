#include "backends/webcam/testing/mock_frame_provider.hpp"

#include <string>
#include <utility>

namespace labops::backends::webcam::testing {

MockFrameProvider::MockFrameProvider(std::vector<WebcamFrameProviderSample> script)
    : script_(std::move(script)) {}

bool MockFrameProvider::Next(const std::uint64_t /*frame_id*/, WebcamFrameProviderSample& sample,
                             std::string& error) {
  if (next_index_ >= script_.size()) {
    error = "mock webcam frame script exhausted";
    return false;
  }
  sample = script_[next_index_++];
  error.clear();
  return true;
}

std::size_t MockFrameProvider::next_index() const {
  return next_index_;
}

std::size_t MockFrameProvider::script_size() const {
  return script_.size();
}

} // namespace labops::backends::webcam::testing
