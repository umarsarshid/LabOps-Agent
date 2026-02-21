# refactor(webcam): prefer Linux V4L2 with OpenCV fallback

## Why this change
Linux webcam behavior still treated OpenCV as the primary availability gate in some paths. That made native-first behavior incomplete and could incorrectly report webcam as unavailable on Linux when OpenCV was off.

This refactor makes Linux native V4L2 the default path and keeps OpenCV as fallback only when needed.

## What was implemented

### 1) Linux platform availability now reflects native V4L2-first design
File:
- `src/backends/webcam/linux/platform_probe_linux.cpp`

Changes:
- Linux probe now reports webcam backend as available through native V4L2 path.
- OpenCV is now reported as fallback context (enabled/disabled), not the gate that decides Linux availability.
- Preserved capability reporting for `pixel_format` and `frame_rate` as `best_effort`.

Why:
- Linux native V4L2 code is compiled independently of OpenCV and should be the default backend path.

### 2) Webcam connect selection logic now enforces native-first + conditional fallback
File:
- `src/backends/webcam/webcam_backend.cpp`

Changes:
- Added session evidence cleanup for `webcam.capture_*` keys.
- Added explicit backend selection evidence key:
  - `webcam.capture_backend = linux_v4l2` when native mmap path is selected
  - `webcam.capture_backend = opencv_fallback` when fallback is used
- Linux connect flow now:
  - tries native V4L2 open first
  - uses native path immediately when mmap streaming is available
  - records fallback reason when native path cannot be used
  - only attempts OpenCV fallback when OpenCV bootstrap is compiled
  - returns actionable `BACKEND_CONNECT_FAILED` when native is unavailable and OpenCV fallback is not compiled
  - returns combined actionable error when native path is unavailable and OpenCV fallback also fails

Why:
- Makes backend choice deterministic and explicit.
- Prevents silent fallback attempts when fallback is not compiled.
- Gives better operator diagnostics in Linux lab environments.

### 3) Smoke coverage tightened for Linux availability expectation
Files:
- `tests/backends/webcam_backend_smoke.cpp`
- `tests/labops/list_backends_smoke.cpp`

Changes:
- Added Linux-only assertion in backend smoke test that platform probe is available via native path.
- Added Linux-only assertion in `list_backends_smoke` to require `webcam ✅ enabled`.

Why:
- Locks the expected Linux availability contract so future changes do not regress native-first behavior.

### 4) Documentation updated for backend-selection behavior
Files:
- `src/backends/webcam/README.md`
- `src/backends/webcam/linux/README.md`

Changes:
- Clarified that Linux prefers native V4L2 and uses OpenCV only as fallback.
- Clarified Linux platform probe semantics and fallback context reporting.

Why:
- Keeps contributor and operator docs aligned with runtime behavior.

## Verification performed

1. Formatting
- `bash tools/clang_format.sh --check`
- Result: passed

2. Configure + build
- `cmake -S . -B build`
- `cmake --build build`
- Result: passed

3. Focused tests
- `ctest --test-dir build -R "webcam_backend_smoke|list_backends_smoke|list_devices_webcam_backend_smoke|run_webcam_selector_resolution_smoke" --output-on-failure`
- Result: passed (`4/4`)

4. Full regression
- `ctest --test-dir build --output-on-failure`
- Result: passed (`83/83`)

## Outcome
On Linux, webcam backend behavior is now explicitly native-first (V4L2) with OpenCV fallback only when needed and available, with clearer diagnostics and stronger regression coverage.
