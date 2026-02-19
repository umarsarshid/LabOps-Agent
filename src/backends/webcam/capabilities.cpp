#include "backends/webcam/capabilities.hpp"

namespace labops::backends::webcam {

const char* ToString(const CapabilityState state) {
  switch (state) {
  case CapabilityState::kUnsupported:
    return "unsupported";
  case CapabilityState::kBestEffort:
    return "best_effort";
  case CapabilityState::kSupported:
    return "supported";
  }
  return "unsupported";
}

} // namespace labops::backends::webcam
