#include "backends/real_sdk/sdk_context.hpp"

#include <utility>

namespace labops::backends::real_sdk {

std::mutex SdkContext::global_mu_{};
bool SdkContext::initialized_ = false;
std::uint32_t SdkContext::active_handles_ = 0;
std::uint64_t SdkContext::init_calls_ = 0;
std::uint64_t SdkContext::shutdown_calls_ = 0;

SdkContext::~SdkContext() {
  Release();
}

SdkContext::SdkContext(SdkContext&& other) noexcept {
  acquired_ = std::exchange(other.acquired_, false);
}

SdkContext& SdkContext::operator=(SdkContext&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Release();
  acquired_ = std::exchange(other.acquired_, false);
  return *this;
}

bool SdkContext::Acquire(std::string& error) {
  if (acquired_) {
    return true;
  }

  std::lock_guard<std::mutex> lock(global_mu_);
  if (!initialized_) {
    if (!InitializeSdk(error)) {
      return false;
    }
    initialized_ = true;
    ++init_calls_;
  }

  ++active_handles_;
  acquired_ = true;
  return true;
}

void SdkContext::Release() {
  if (!acquired_) {
    return;
  }

  std::lock_guard<std::mutex> lock(global_mu_);
  if (active_handles_ > 0U) {
    --active_handles_;
  }
  acquired_ = false;

  if (active_handles_ == 0U && initialized_) {
    ShutdownSdk();
    initialized_ = false;
    ++shutdown_calls_;
  }
}

SdkContext::Snapshot SdkContext::DebugSnapshot() {
  std::lock_guard<std::mutex> lock(global_mu_);
  return Snapshot{
      .initialized = initialized_,
      .active_handles = active_handles_,
      .init_calls = init_calls_,
      .shutdown_calls = shutdown_calls_,
  };
}

void SdkContext::DebugResetForTests() {
  std::lock_guard<std::mutex> lock(global_mu_);
  initialized_ = false;
  active_handles_ = 0;
  init_calls_ = 0;
  shutdown_calls_ = 0;
}

bool SdkContext::InitializeSdk(std::string& error) {
  // Placeholder for proprietary SDK global init. This remains deterministic in
  // OSS builds so real-backend wiring can be validated on every platform.
  error.clear();
  return true;
}

void SdkContext::ShutdownSdk() {
  // Placeholder for proprietary SDK global shutdown.
}

} // namespace labops::backends::real_sdk
