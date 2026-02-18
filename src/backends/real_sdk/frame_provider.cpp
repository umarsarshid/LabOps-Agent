#include "backends/real_sdk/frame_provider.hpp"

#include <algorithm>

namespace labops::backends::real_sdk {

namespace {

constexpr std::uint64_t kSplitMixIncrement = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t kOutcomeSalt = 0x8b8b8b8b8b8b8b8bULL;

std::uint64_t SplitMix64(std::uint64_t value) {
  std::uint64_t state = value + kSplitMixIncrement;
  state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
  state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
  return state ^ (state >> 31);
}

FrameOutcome DetermineOutcome(std::uint64_t seed, std::uint64_t frame_id, double timeout_percent,
                              double incomplete_percent) {
  if (timeout_percent <= 0.0 && incomplete_percent <= 0.0) {
    return FrameOutcome::kReceived;
  }

  // Deterministic sample in [0, 100) so seeded runs remain reproducible.
  const std::uint64_t mixed = SplitMix64((seed ^ kOutcomeSalt) + frame_id * kSplitMixIncrement);
  const double sample_percent = static_cast<double>(mixed % 100'000ULL) / 1'000.0;
  if (sample_percent < timeout_percent) {
    return FrameOutcome::kTimeout;
  }
  if (sample_percent < timeout_percent + incomplete_percent) {
    return FrameOutcome::kIncomplete;
  }
  return FrameOutcome::kReceived;
}

} // namespace

DeterministicFrameProvider::DeterministicFrameProvider(const std::uint64_t seed,
                                                       const std::uint32_t frame_size_bytes,
                                                       const double timeout_percent,
                                                       const double incomplete_percent)
    : seed_(seed), frame_size_bytes_(frame_size_bytes), timeout_percent_(timeout_percent),
      incomplete_percent_(incomplete_percent) {}

bool DeterministicFrameProvider::Next(const std::uint64_t frame_id, FrameProviderSample& sample,
                                      std::string& error) {
  error.clear();
  sample = FrameProviderSample{};
  sample.outcome = DetermineOutcome(seed_, frame_id, timeout_percent_, incomplete_percent_);

  switch (sample.outcome) {
  case FrameOutcome::kTimeout:
    sample.size_bytes = 0U;
    break;
  case FrameOutcome::kIncomplete:
    sample.size_bytes = std::max<std::uint32_t>(1U, frame_size_bytes_ / 4U);
    break;
  case FrameOutcome::kDropped:
    sample.size_bytes = 0U;
    break;
  case FrameOutcome::kReceived:
  default:
    sample.size_bytes = frame_size_bytes_;
    break;
  }

  return true;
}

} // namespace labops::backends::real_sdk
