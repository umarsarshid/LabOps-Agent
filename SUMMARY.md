# 0115 — Webcam Linux Teardown Correctness (No Device Busy)

## Goal
Ensure Linux webcam runs tear down cleanly and predictably so repeated runs do not leave `/dev/video*` wedged as busy.

## What Was Implemented

### 1) Hardened `V4l2CaptureDevice::Close` to always attempt fd close
Files:
- `src/backends/webcam/linux/v4l2_capture_device.cpp`

Change:
- `Close()` no longer returns early when `StopStreaming()` reports an error.
- It now:
  - attempts `StopStreaming()` first,
  - still attempts `close(fd)` even if stop/unmap/release had an error,
  - clears in-memory capture state if fd close succeeds,
  - returns combined actionable error text when stop and/or close fail.

Why:
- Previously, a stream teardown error could skip `close(fd)`, which is exactly the pattern that can cause "device busy" on the next run.
- Closing the descriptor is the critical release boundary; we now prioritize attempting that boundary every time.

### 2) Added explicit RAII cleanup for webcam backend lifetime
Files:
- `src/backends/webcam/webcam_backend.hpp`
- `src/backends/webcam/webcam_backend.cpp`

Change:
- Added `~WebcamBackend()` destructor.
- Added `TeardownSessionBestEffort()` helper that:
  - for Linux-native path: best-effort `StopStreaming()` + `Close()`,
  - otherwise: best-effort `OpenCV::CloseDevice()`,
  - clears `running_/connected_/linux_native_capture_selected_` flags.

Why:
- Ensures session resources are released even on early exits, exceptions, or interrupted flow paths where explicit stop calls might not complete cleanly.
- Makes teardown behavior object-lifetime-safe and less dependent on control-flow success.

### 3) Hardened Linux-native `WebcamBackend::Stop`
Files:
- `src/backends/webcam/webcam_backend.cpp`

Change:
- `Stop()` for Linux-native sessions now:
  - attempts `StopStreaming()`,
  - always attempts `Close()` afterward,
  - clears session flags regardless,
  - returns precise error messaging for stop-only, close-only, or combined teardown failures.

Why:
- Prevents internal "still running/connected" wedge states after partial teardown failures.
- Improves repeatability by guaranteeing the backend can attempt a fresh reconnect on the next run.

### 4) Added regression smoke coverage for stop-failure close path
Files:
- `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`

Added test:
- `TestCloseStillClosesFdWhenStreamStopFails()`

What it asserts:
- if `VIDIOC_STREAMOFF` fails,
- `Close()` still calls fd close,
- device reports not-open afterward,
- subsequent close is idempotent.

Why:
- Directly guards the no-device-busy fix with deterministic Linux fake-IO coverage.

### 5) Module docs updated to reflect teardown guarantees
Files:
- `src/backends/webcam/linux/README.md`
- `src/backends/webcam/README.md`

Change:
- Documented fail-safe teardown behavior and RAII cleanup expectations.

Why:
- Keeps future contributors aligned with the intended lifecycle contract and reduces regressions during future backend changes.

## Verification Performed

1. Formatting gate:
- `bash tools/clang_format.sh --check`

2. Build:
- `cmake --build build`

3. Focused tests for touched behavior:
- `ctest --test-dir build -R "webcam_linux_v4l2_capture_device_smoke|webcam_backend_smoke|run_webcam_selector_resolution_smoke|run_interrupt_flush_smoke" --output-on-failure`

4. Full regression suite:
- `ctest --test-dir build --output-on-failure`
- Result: `81/81` passed.

## Outcome
Teardown path now prioritizes descriptor release even during partial streaming teardown failures, backend lifecycle has explicit best-effort RAII cleanup, and repeated runs are protected by deterministic regression tests against the previously risky path.
