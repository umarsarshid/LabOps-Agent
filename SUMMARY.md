# SUMMARY — 0094 `test(real): add MockFrameProvider tests for acquisition loop`

## Goal
Make the real acquisition loop independently testable in CI without cameras/SDK, while validating timeout/incomplete/stall behavior and event/counter outcomes.

## Implementation

### 1) Added a frame-provider abstraction
Files:
- `src/backends/real_sdk/frame_provider.hpp`
- `src/backends/real_sdk/frame_provider.cpp`

What:
- Introduced `IFrameProvider` interface to decouple frame sourcing from loop orchestration.
- Added `FrameProviderSample` with:
  - `outcome`
  - `size_bytes`
  - `stall_periods` (synthetic burst stall in frame periods)
- Added `DeterministicFrameProvider` that preserves current OSS real-backend deterministic outcome behavior (`received`, `timeout`, `incomplete`) via seed + percentages.

Why:
- Enables true mock-driven loop tests without requiring vendor SDK hooks.
- Keeps frame generation policy separate from stream-loop mechanics.

### 2) Added an acquisition-loop module with counters/event classification
Files:
- `src/backends/real_sdk/acquisition_loop.hpp`
- `src/backends/real_sdk/acquisition_loop.cpp`

What:
- Added `RunAcquisitionLoop(...)` orchestration function with explicit input/result contracts.
- Added `AcquisitionLoopCounters`:
  - `frames_total`
  - `frames_received`
  - `frames_dropped`
  - `frames_timeout`
  - `frames_incomplete`
  - `stall_periods_total`
- Added event-like classification for test assertions:
  - `kFrameReceived`
  - `kFrameDropped`
  - `kFrameTimeout`
  - `kFrameIncomplete`
- Added timestamp handling with synthetic stall-period offsets and monotonic guard.

Why:
- Provides a deterministic, testable stream loop contract independent of hardware.
- Makes timeout/incomplete/stall behavior explicit and assertable.

### 3) Refactored `RealBackend::PullFrames` to use the loop
File:
- `src/backends/real_sdk/real_backend.cpp`

What:
- Replaced inline frame-generation loop with:
  - `DeterministicFrameProvider`
  - `RunAcquisitionLoop(...)`
- Preserved validation and existing parameter resolution behavior.
- Preserved deterministic semantics, while emitting richer sdk-log counts including stall periods.

Why:
- Ensures production real-backend pull path uses the same loop now covered by mock-based tests.
- Avoids “tested code path” drift.

### 4) Added new CI-safe mock-provider acquisition test
File:
- `tests/backends/real_acquisition_loop_mock_provider_smoke.cpp`

What this test simulates:
- timeout frames
- incomplete frames
- burst stalls (`stall_periods` injected as 3 + 2)

Assertions include:
- frame/event vector alignment
- counter correctness (received/timeout/incomplete/dropped/stall)
- normalized timeout/incomplete frame properties
- timestamp monotonicity
- large timestamp gaps from burst stalls

Why:
- Directly validates stream-loop behavior with no camera and no SDK.
- Satisfies milestone intent: confidence in CI without hardware.

### 5) Build/docs/test wiring updates
Files:
- `CMakeLists.txt`
- `src/backends/real_sdk/README.md`
- `tests/backends/README.md`

What:
- Added new backends sources (`acquisition_loop.cpp`, `frame_provider.cpp`).
- Added new smoke target: `real_acquisition_loop_mock_provider_smoke`.
- Documented new modules/tests and their role.

Why:
- Keeps maintainers oriented and ensures CI executes new coverage.

### 6) Formatting hygiene adjustment
File:
- `tests/backends/mock_node_map_adapter_smoke.cpp`

What:
- Applied clang-format-compliant wrapping (format-only).

Why:
- Required to keep repo-wide `clang-format --check` green.

## Verification

Formatting:
- `bash tools/clang_format.sh --check` → pass

Build:
- `cmake --build build` → pass

Targeted tests:
- `ctest --test-dir build -R "real_acquisition_loop_mock_provider_smoke|real_frame_acquisition_smoke|run_stream_trace_smoke" --output-on-failure` → pass

Full regression:
- `ctest --test-dir build --output-on-failure` → pass (`66/66`)

## Outcome
- Real stream-loop behavior is now directly testable with a mock provider.
- Timeout/incomplete/burst-stall scenarios are CI-covered without hardware.
- Events + counters are explicitly asserted in tests.
