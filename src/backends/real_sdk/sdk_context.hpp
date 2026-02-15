#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace labops::backends::real_sdk {

// Global SDK lifecycle guard for real-backend integration.
//
// Why this exists:
// - vendor SDKs usually require one process-level init/shutdown pair
// - multiple backend instances may coexist (tests, retries, agent runs)
// - this RAII handle guarantees balanced acquire/release and safe teardown
class SdkContext {
public:
  SdkContext() = default;
  ~SdkContext();

  SdkContext(const SdkContext&) = delete;
  SdkContext& operator=(const SdkContext&) = delete;
  SdkContext(SdkContext&& other) noexcept;
  SdkContext& operator=(SdkContext&& other) noexcept;

  // Acquires global SDK context for this handle. Idempotent per instance.
  bool Acquire(std::string& error);

  // Releases this handle's acquisition if present. Safe to call repeatedly.
  void Release();

  bool acquired() const {
    return acquired_;
  }

  // Snapshot is used by smoke tests to verify one-time init and balanced
  // shutdown behavior in deterministic OSS builds.
  struct Snapshot {
    bool initialized = false;
    std::uint32_t active_handles = 0;
    std::uint64_t init_calls = 0;
    std::uint64_t shutdown_calls = 0;
  };

  static Snapshot DebugSnapshot();
  static void DebugResetForTests();

private:
  static bool InitializeSdk(std::string& error);
  static void ShutdownSdk();

  bool acquired_ = false;

  static std::mutex global_mu_;
  static bool initialized_;
  static std::uint32_t active_handles_;
  static std::uint64_t init_calls_;
  static std::uint64_t shutdown_calls_;
};

} // namespace labops::backends::real_sdk
