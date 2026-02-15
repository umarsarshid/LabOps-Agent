#include "backends/real_sdk/sdk_context.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Fail(std::string_view message) {
  std::cerr << message << '\n';
  std::abort();
}

void AssertState(const labops::backends::real_sdk::SdkContext::Snapshot& snapshot, bool initialized,
                 std::uint32_t active_handles, std::uint64_t init_calls,
                 std::uint64_t shutdown_calls) {
  if (snapshot.initialized != initialized || snapshot.active_handles != active_handles ||
      snapshot.init_calls != init_calls || snapshot.shutdown_calls != shutdown_calls) {
    std::cerr << "unexpected sdk context state:"
              << " initialized=" << (snapshot.initialized ? "true" : "false")
              << " active_handles=" << snapshot.active_handles
              << " init_calls=" << snapshot.init_calls
              << " shutdown_calls=" << snapshot.shutdown_calls << '\n';
    std::abort();
  }
}

} // namespace

int main() {
  using labops::backends::real_sdk::SdkContext;

  SdkContext::DebugResetForTests();
  AssertState(SdkContext::DebugSnapshot(), false, 0U, 0U, 0U);

  std::string error;
  SdkContext a;
  if (!a.Acquire(error)) {
    Fail("first acquire should succeed");
  }
  AssertState(SdkContext::DebugSnapshot(), true, 1U, 1U, 0U);

  SdkContext b;
  if (!b.Acquire(error)) {
    Fail("second acquire should succeed");
  }
  AssertState(SdkContext::DebugSnapshot(), true, 2U, 1U, 0U);

  b.Release();
  AssertState(SdkContext::DebugSnapshot(), true, 1U, 1U, 0U);

  a.Release();
  AssertState(SdkContext::DebugSnapshot(), false, 0U, 1U, 1U);

  // Releasing again should be a safe no-op.
  a.Release();
  AssertState(SdkContext::DebugSnapshot(), false, 0U, 1U, 1U);

  {
    SdkContext c;
    if (!c.Acquire(error)) {
      Fail("third acquire should succeed after prior shutdown");
    }
    AssertState(SdkContext::DebugSnapshot(), true, 1U, 2U, 1U);
  }
  AssertState(SdkContext::DebugSnapshot(), false, 0U, 2U, 2U);

  return 0;
}
