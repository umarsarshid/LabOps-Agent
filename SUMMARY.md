# LabOps Summary

## Commit: refactor(webcam): add monotonic capture clock layer

Date: 2026-02-20

### Goal
Implement milestone `0107` by introducing a monotonic timing layer for webcam capture internals while keeping output/event timestamp contracts unchanged.

Required outcome:
- webcam backend uses monotonic timing internally
- event/metrics artifacts stay contract-compliant (`system_clock` timestamps)

### Implemented

1. Added a dedicated webcam `CaptureClock` module
- Added:
  - `src/backends/webcam/capture_clock.hpp`
  - `src/backends/webcam/capture_clock.cpp`
- `CaptureClock` provides:
  - monotonic anchor (`steady_clock`) + wall anchor (`system_clock`)
  - conversion `ToWallTime(steady_ts)`
  - `ResetToNow()` for live capture sessions
  - explicit `Anchored(...)` factory for deterministic test scenarios

Why:
- centralizes monotonic-to-wall conversion into one reusable place
- prevents ad-hoc timestamp stamping from drifting across webcam paths

2. Wired `OpenCvWebcamImpl` to use monotonic capture timing internally
- Updated `src/backends/webcam/opencv_webcam_impl.cpp`:
  - added `CaptureClock` state in `Impl`
  - test mode now tracks:
    - `test_stream_start_steady`
    - `test_stream_start_wall`
  - frame timestamps now come from steady capture time converted by `CaptureClock`
- Real OpenCV pull loop change:
  - before: `sample.timestamp = std::chrono::system_clock::now()`
  - now: use `read_finished_at` (steady) and convert via `capture_clock`

Why:
- steady clock avoids wall-clock jumps during capture loops
- conversion preserves existing contract output type and downstream behavior

3. Preserved deterministic test-mode behavior with monotonic internals
- Test mode now anchors `CaptureClock` with explicit wall+steady anchors
- Scripted stall gaps still produce deterministic timestamp jumps

Why:
- keeps current deterministic mock-provider tests stable
- enforces monotonic internal timing even in test mode

4. Added focused smoke test for capture-clock contract
- Added `tests/backends/webcam_capture_clock_smoke.cpp`
- Validates:
  - anchor equivalence
  - positive/negative delta mapping accuracy
  - non-backwards behavior for increasing steady times

Why:
- provides direct regression coverage for the new timing layer
- ensures future refactors do not break contract-safe conversion

5. Build wiring + docs updates
- Updated `CMakeLists.txt`:
  - include `capture_clock.cpp` in `labops_backends`
  - register `webcam_capture_clock_smoke`
- Updated readmes:
  - `src/backends/webcam/README.md`
  - `tests/backends/README.md`
- Updated `src/backends/webcam/opencv_webcam_impl.hpp` comments to describe monotonic stamping intent.

Why:
- keeps module/test documentation aligned with implementation
- makes timestamp architecture clear for future contributors

### Files changed
- `CMakeLists.txt`
- `src/backends/webcam/capture_clock.hpp`
- `src/backends/webcam/capture_clock.cpp`
- `src/backends/webcam/opencv_webcam_impl.hpp`
- `src/backends/webcam/opencv_webcam_impl.cpp`
- `src/backends/webcam/README.md`
- `tests/backends/webcam_capture_clock_smoke.cpp`
- `tests/backends/README.md`
- `SUMMARY.md`

### Verification

1. Formatting
- Command: `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- Command: `cmake --build build`
- Result: passed

3. Focused timing/webcam regression
- Command:
  - `ctest --test-dir build -R "webcam_capture_clock_smoke|webcam_opencv_mock_provider_smoke|webcam_backend_smoke|run_webcam_selector_resolution_smoke|run_stream_trace_smoke" --output-on-failure`
- Result: passed (`5/5`)

4. Full suite regression
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`79/79`)

### Outcome
Webcam timing is now monotonic internally through a dedicated `CaptureClock` layer, while all external artifacts/events remain in the existing `system_clock` contract format.
