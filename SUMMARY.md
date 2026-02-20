# LabOps Summary

## Commit: feat(webcam/linux): add V4L2 mmap streaming bootstrap

Date: 2026-02-20

### Goal
Implement milestone `0113` by adding Linux-native V4L2 mmap streaming startup lifecycle:
- `VIDIOC_REQBUFS`
- `VIDIOC_QUERYBUF`
- `mmap`
- `VIDIOC_QBUF`
- `VIDIOC_STREAMON`

Done criteria:
- native mmap stream startup path exists and is test-covered
- startup/teardown errors are explicit and actionable
- webcam backend captures Linux stream-start evidence during connect

### What was implemented

1. Added mmap streaming lifecycle API to Linux V4L2 capture helper
- Updated: `src/backends/webcam/linux/v4l2_capture_device.hpp`
- Updated: `src/backends/webcam/linux/v4l2_capture_device.cpp`

New contracts:
- `V4l2StreamStartInfo`
- `StartMmapStreaming(requested_buffer_count, stream_info, error)`
- `StopStreaming(error)`
- `IsStreaming()`

New internal state:
- mapped buffer table (`address`, `length`)
- buffer-allocation state flag
- streaming state flag

Why:
- open/query capability checks are not enough for real capture readiness.
- this adds the real kernel handshake for streaming so Linux webcam readiness is proven, not assumed.

2. Extended IO abstraction to support mmap/munmap in production and tests
- Updated: `V4l2CaptureDevice::IoOps`
  - added `mmap_fn`
  - added `munmap_fn`
- default Linux IO ops now bind to:
  - `::mmap`
  - `::munmap`

Why:
- keeps Linux runtime behavior and no-hardware tests aligned.
- allows deterministic mmap lifecycle testing in CI without real webcams.

3. Implemented robust startup and cleanup behavior
- `StartMmapStreaming(...)` now:
  - validates device is open and method is `mmap_streaming`
  - requests buffers via `VIDIOC_REQBUFS`
  - queries each buffer via `VIDIOC_QUERYBUF`
  - maps each buffer via `mmap`
  - queues each buffer via `VIDIOC_QBUF`
  - starts stream via `VIDIOC_STREAMON`
- `StopStreaming(...)` now:
  - stops stream via `VIDIOC_STREAMOFF` when active
  - unmaps buffers via `munmap`
  - releases kernel buffers via `VIDIOC_REQBUFS(count=0)`
- `Close(...)` now enforces stream cleanup before FD close.

Why:
- avoids leaked mappings/buffers and keeps repeated runs stable.
- makes failure modes actionable (`REQBUFS`, `QUERYBUF`, `QBUF`, `STREAMON`, `STREAMOFF`, `munmap` each report clear context).

4. Wired Linux mmap stream-start probe evidence into webcam backend connect flow
- Updated: `src/backends/webcam/webcam_backend.cpp`

Behavior on Linux during connect:
- after native open + best-effort format apply, backend now tries mmap stream bootstrap probe.
- on success records:
  - `webcam.linux_capture.stream_start=ok`
  - `webcam.linux_capture.stream_buffer_count`
  - `webcam.linux_capture.stream_buffer_type`
- on non-mmap devices records explicit skip reason.
- on start failure records explicit `stream_start_error` evidence (best-effort, non-fatal to OpenCV bootstrap flow).

Why:
- surfaces real Linux stream-readiness evidence in bundle/config diagnostics.
- keeps current OpenCV bootstrap path working while native Linux path is built incrementally.

5. Added deterministic no-hardware streaming smoke coverage
- Updated: `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`

Added test harness support for:
- `VIDIOC_REQBUFS`
- `VIDIOC_QUERYBUF`
- `VIDIOC_QBUF`
- `VIDIOC_STREAMON`
- `VIDIOC_STREAMOFF`
- fake `mmap`/`munmap`

New tests:
- `TestMmapStreamingStartStop`
  - validates full startup and teardown call sequence/counters
- `TestMmapStreamingFailureIsActionable`
  - validates actionable error when `REQBUFS` fails
- `TestMmapStreamingRejectsReadFallbackDevices`
  - validates mmap start is rejected when device is read-fallback only

Why:
- protects the exact stream startup contract in CI without camera dependencies.

6. Updated module docs to reflect new Linux streaming bootstrap stage
- Updated:
  - `src/backends/webcam/linux/README.md`
  - `src/backends/webcam/README.md`
  - `tests/backends/README.md`

Why:
- keeps contributor handoff docs aligned with real implementation.

### Files changed
- `src/backends/webcam/linux/v4l2_capture_device.hpp`
- `src/backends/webcam/linux/v4l2_capture_device.cpp`
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

3. Focused webcam tests
- Command:
  - `ctest --test-dir build -R "webcam_linux_v4l2_capture_device_smoke|webcam_backend_smoke|run_webcam_selector_resolution_smoke|list_devices_webcam_backend_smoke" --output-on-failure`
- Result: passed (`4/4`)

4. Full regression suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`81/81`)

### Outcome
Linux webcam backend now includes real mmap streaming bootstrap lifecycle with explicit evidence and deterministic CI coverage, which materially improves confidence that streaming can actually start on Linux before the full native frame acquisition loop is introduced.
