# LabOps Summary

## Commit: feat(webcam/linux): add V4L2 open and capture-method selection

Date: 2026-02-20

### Goal
Implement milestone `0111` by adding Linux-native V4L2 device open/close handling and deterministic capture-path selection:
- prefer `mmap` streaming
- fallback to `read()` when streaming is unavailable

Done criteria:
- open/close lifecycle is stable
- failures are explicit/actionable

### What was implemented

1. Added native Linux V4L2 open/close helper module
- Added: `src/backends/webcam/linux/v4l2_capture_device.hpp`
- Added: `src/backends/webcam/linux/v4l2_capture_device.cpp`

New contracts:
- `V4l2CaptureDevice::Open(...)`
- `V4l2CaptureDevice::Close(...)`
- `V4l2CaptureDevice::ChooseCaptureMethod(...)`
- `V4l2OpenInfo` evidence payload

Behavior:
- opens device descriptor with non-blocking flags
- validates `VIDIOC_QUERYCAP`
- computes effective capabilities (`device_caps` fallback to `capabilities`)
- validates capture capability presence
- selects capture method with deterministic rules:
  - if streaming is present -> `mmap_streaming`
  - else if read/write is present -> `read_fallback`
  - else fail with explicit reason
- returns clear errors for:
  - open failure
  - querycap failure
  - unsupported capability set
  - close failure
- close is idempotent (`Close()` on already closed device returns success)

Why:
- this isolates Linux descriptor lifecycle from generic backend flow.
- this gives one source of truth for method selection and error text.
- this lays the foundation for full native Linux frame acquisition in later commits.

2. Wired Linux native probe into webcam backend connect path (best-effort evidence)
- Updated: `src/backends/webcam/webcam_backend.hpp`
- Updated: `src/backends/webcam/webcam_backend.cpp`

Behavior in `Connect(...)` on Linux:
- probes `/dev/video<index>` through `V4l2CaptureDevice`
- on success records evidence into backend config:
  - `webcam.linux_capture.path`
  - `webcam.linux_capture.driver`
  - `webcam.linux_capture.card`
  - `webcam.linux_capture.capabilities_hex`
  - `webcam.linux_capture.method`
  - `webcam.linux_capture.method_reason`
- on probe failure records:
  - `webcam.linux_capture.path`
  - `webcam.linux_capture.error`
- keeps current OpenCV bootstrap run path unchanged

Why:
- exposes native open/method-selection decisions in artifacts immediately.
- keeps existing run behavior stable while native capture pipeline is built incrementally.

3. Added deterministic smoke test for V4L2 open/close + method selection
- Added: `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`
- Updated: `CMakeLists.txt` (new backend source + smoke target)

Test coverage (no hardware required):
- mmap preference when both streaming + read are available
- read fallback when streaming is unavailable
- actionable failures for:
  - open syscall failure
  - `VIDIOC_QUERYCAP` failure
  - no capture capability
  - no usable capture method
  - close failure
- idempotent close behavior

Why:
- validates lifecycle and error contract in CI without depending on real webcams.

4. Updated module/test docs
- Updated:
  - `src/backends/webcam/linux/README.md`
  - `src/backends/webcam/README.md`
  - `tests/backends/README.md`

Why:
- keeps implementation details and expected behavior clear for future contributors.

### Files changed
- `CMakeLists.txt`
- `src/backends/webcam/linux/v4l2_capture_device.hpp`
- `src/backends/webcam/linux/v4l2_capture_device.cpp`
- `src/backends/webcam/webcam_backend.hpp`
- `src/backends/webcam/webcam_backend.cpp`
- `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`
- `src/backends/webcam/linux/README.md`
- `src/backends/webcam/README.md`
- `tests/backends/README.md`
- `SUMMARY.md`

### Verification

1. Formatting
- Command: `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- Command: `cmake --build build`
- Result: passed

3. Focused tests
- Command:
  - `ctest --test-dir build -R "webcam_linux_v4l2_capture_device_smoke|webcam_backend_smoke|list_devices_webcam_backend_smoke|webcam_device_selector_smoke|run_webcam_selector_resolution_smoke" --output-on-failure`
- Result: passed (`5/5`)

4. Full regression suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`81/81`)

### Outcome
LabOps now has a native Linux V4L2 open/close lifecycle module with explicit capture-method selection (`mmap` preferred, `read` fallback) and actionable failure diagnostics, plus deterministic tests protecting this behavior.
