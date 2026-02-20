# LabOps Summary

## Commit: test(webcam): add deterministic mock provider mode

Date: 2026-02-20

### Goal
Implement webcam milestone `0105` by adding a deterministic frame-provider path for webcam OpenCV tests so CI/tests do not depend on physical cameras or OpenCV capture behavior.

Done criteria targeted:
- no OpenCV camera dependency in tests
- deterministic simulation of steady frames, timeout gaps, and incomplete frames
- wired behind webcam impl test mode
- ctest can validate timeout/incomplete classification deterministically

### Implemented

1. Added a test-mode frame provider contract to OpenCV webcam implementation
- Updated `src/backends/webcam/opencv_webcam_impl.hpp`
- Added:
  - `WebcamFrameProviderSample`
  - `IWebcamFrameProvider`
  - `EnableTestMode(...)`
  - `DisableTestMode()`
  - `IsTestModeEnabled()`

Why:
- creates a clean seam for deterministic frame scripts
- keeps production OpenCV runtime path intact while enabling hardware-free tests

2. Implemented deterministic test-mode execution path in `OpenCvWebcamImpl`
- Updated `src/backends/webcam/opencv_webcam_impl.cpp`
- Added internal test-mode state:
  - provider pointer
  - open/closed state
  - deterministic frame period/start timestamp/cursor
  - mock property/fourcc readback values
- Updated behavior in test mode for:
  - `OpenDevice` / `CloseDevice` / `IsDeviceOpen`
  - `SetProperty` / `GetProperty`
  - `SetFourcc` / `GetFourcc`
  - `PullFrames`
- `PullFrames` now can deterministically emit scripted outcomes:
  - `kReceived`
  - `kTimeout`
  - `kIncomplete`
  - `kDropped`
- Added deterministic stall-gap support via `stall_periods` to simulate cadence cliffs.

Why:
- validates frame outcome classification logic with stable reproducible data
- avoids flakiness from camera access, permissions, and driver timing differences

3. Added deterministic scripted mock provider module
- Added:
  - `src/backends/webcam/testing/mock_frame_provider.hpp`
  - `src/backends/webcam/testing/mock_frame_provider.cpp`
- `MockFrameProvider` feeds scripted samples in order and reports exhaustion clearly.

Why:
- central reusable test fixture for webcam backend tests
- simple deterministic primitive for future timeout/incomplete/stall scenarios

4. Added dedicated smoke test for deterministic webcam classification
- Added `tests/backends/webcam_opencv_mock_provider_smoke.cpp`
- Verifies:
  - test mode open/close lifecycle
  - property/fourcc readback in test mode
  - deterministic frame count and frame id progression
  - exact counts for received/timeout/incomplete
  - dropped flags and size semantics
  - deterministic timestamp gap from `stall_periods`

Why:
- proves classification behavior in a stable way without requiring real hardware
- prevents regressions in timeout/incomplete handling

5. Wired new module and test into CMake
- Updated `CMakeLists.txt`
- Added `mock_frame_provider.cpp` to `labops_backends`
- Added smoke test target `webcam_opencv_mock_provider_smoke`

Why:
- ensures deterministic test path is built and executed in the regular test workflow

6. Updated module docs/readmes for handoff clarity
- Updated:
  - `src/backends/webcam/README.md`
  - `src/backends/webcam/testing/README.md`
  - `tests/backends/README.md`

Why:
- documents test-mode intent and how it connects to CI/hardware-free verification

### Files changed
- `CMakeLists.txt`
- `src/backends/webcam/README.md`
- `src/backends/webcam/opencv_webcam_impl.hpp`
- `src/backends/webcam/opencv_webcam_impl.cpp`
- `src/backends/webcam/testing/README.md`
- `src/backends/webcam/testing/mock_frame_provider.hpp`
- `src/backends/webcam/testing/mock_frame_provider.cpp`
- `tests/backends/README.md`
- `tests/backends/webcam_opencv_mock_provider_smoke.cpp`
- `SUMMARY.md`

### Verification

1. Formatting
- Command: `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- Command: `cmake --build build`
- Result: passed

3. Focused webcam tests
- Command:
  - `ctest --test-dir build -R "webcam_opencv_mock_provider_smoke|webcam_backend_smoke|webcam_device_selector_smoke|run_webcam_selector_resolution_smoke" --output-on-failure`
- Result: passed (`4/4`)

4. Full regression suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`78/78`)

### Outcome
The webcam backend now has a deterministic, hardware-independent test path that validates timeout/incomplete classification and cadence-gap behavior reliably in CI/local tests, without changing production runtime behavior.
