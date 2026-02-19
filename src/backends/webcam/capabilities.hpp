#pragma once

namespace labops::backends::webcam {

// Coarse capability states used by the webcam backend to report what control
// surfaces are writable today. This keeps the contract explicit while platform
// implementations are added incrementally.
enum class CapabilityState {
  kUnsupported = 0,
  kBestEffort,
  kSupported,
};

struct CapabilityModel {
  CapabilityState exposure = CapabilityState::kUnsupported;
  CapabilityState gain = CapabilityState::kUnsupported;
  CapabilityState pixel_format = CapabilityState::kUnsupported;
  CapabilityState roi = CapabilityState::kUnsupported;
  CapabilityState trigger = CapabilityState::kUnsupported;
  CapabilityState frame_rate = CapabilityState::kUnsupported;
};

const char* ToString(CapabilityState state);

} // namespace labops::backends::webcam
