#pragma once

#include "backends/camera_backend.hpp"

#include <cstdint>
#include <string>

namespace labops::backends::real_sdk {

// Provider-level sample used by the acquisition loop.
//
// Why this exists:
// - lets tests drive loop behavior without requiring real SDK calls
// - isolates frame-source policy (deterministic/mock/vendor) from loop control
struct FrameProviderSample {
  FrameOutcome outcome = FrameOutcome::kReceived;
  std::uint32_t size_bytes = 0;

  // Optional synthetic stall expressed in frame periods.
  // Example: `stall_periods=3` means insert a gap of 3 extra frame intervals
  // before this sample timestamp.
  std::uint32_t stall_periods = 0;
};

class IFrameProvider {
public:
  virtual ~IFrameProvider() = default;

  // Produces one provider sample for the requested absolute frame id.
  virtual bool Next(std::uint64_t frame_id, FrameProviderSample& sample, std::string& error) = 0;
};

// Deterministic provider used by the OSS real-backend implementation.
//
// It reproduces the prior frame-outcome behavior (received/timeout/incomplete)
// but through the provider interface so loop mechanics can be unit-tested.
class DeterministicFrameProvider final : public IFrameProvider {
public:
  DeterministicFrameProvider(std::uint64_t seed, std::uint32_t frame_size_bytes,
                             double timeout_percent, double incomplete_percent);

  bool Next(std::uint64_t frame_id, FrameProviderSample& sample, std::string& error) override;

private:
  std::uint64_t seed_ = 1U;
  std::uint32_t frame_size_bytes_ = 4'096U;
  double timeout_percent_ = 1.0;
  double incomplete_percent_ = 1.0;
};

} // namespace labops::backends::real_sdk
