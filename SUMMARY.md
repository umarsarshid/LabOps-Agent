# LabOps Summary

## Commit: feat(webcam/linux): add poll+dqbuf+requeue frame acquisition

Date: 2026-02-20

### Goal
Implement milestone `0114` for Linux webcam streaming:
- frame acquisition loop using `poll()` + `VIDIOC_DQBUF` + `VIDIOC_QBUF`
- timeout classification (`FRAME_TIMEOUT`)
- dequeue validation for incomplete payloads (`FRAME_INCOMPLETE`)
- monotonic internal frame timestamps
- clean stream shutdown after runs

Done criteria:
- 10s webcam run can produce frame events and metrics using Linux native mmap path
- timeout/received/incomplete outcomes flow through existing event and metric pipelines
- streaming teardown remains clean and deterministic

### What was implemented

1. Added Linux native frame pull API to V4L2 capture helper
- Updated: `src/backends/webcam/linux/v4l2_capture_device.hpp`
- Updated: `src/backends/webcam/linux/v4l2_capture_device.cpp`

New contracts:
- `V4l2FrameOutcome` (`kReceived`, `kTimeout`, `kIncomplete`)
- `V4l2FrameSample` (includes monotonic `captured_at_steady`)
- `PullFrames(duration, next_frame_id, frames, error)`

Behavior:
- requires open + active streaming session
- uses bounded `poll()` timeout budget per iteration
- on `poll` timeout emits timeout sample
- on dequeue:
  - checks `bytesused`
  - checks `V4L2_BUF_FLAG_ERROR`
  - classifies as received vs incomplete
- always requeues dequeued buffers via `VIDIOC_QBUF`
- keeps frame timestamps in monotonic `steady_clock`

Why:
- this is the core real-time acquisition loop needed for Linux webcam runs.
- timeout and incomplete must be separated because engineers triage them differently.
- monotonic timing prevents wall-clock jumps from corrupting frame timing behavior.

2. Extended V4L2 IO abstraction for deterministic acquisition testing
- `IoOps` now includes:
  - `poll_fn`
  - `steady_now_fn`

Why:
- enables deterministic no-hardware tests for timeout/dequeue behavior without relying on actual device timing.

3. Wired Linux native capture as primary webcam run path when mmap is available
- Updated: `src/backends/webcam/webcam_backend.hpp`
- Updated: `src/backends/webcam/webcam_backend.cpp`

Behavior changes:
- during `Connect` on Linux:
  - if V4L2 mmap path is available, backend selects native capture path
  - requested format/readback evidence is still recorded
  - stream startup is deferred to `Start`
- during `Start` on Linux native path:
  - starts mmap streaming and records stream evidence
- during `PullFrames` on Linux native path:
  - calls V4L2 native pull loop
  - converts monotonic timestamps to wall timestamps through `CaptureClock`
  - maps native outcomes to shared backend outcomes:
    - `kTimeout` -> `FrameOutcome::kTimeout`
    - `kIncomplete` -> `FrameOutcome::kIncomplete`
    - `kReceived` -> `FrameOutcome::kReceived`
- during `Stop` on Linux native path:
  - stops streaming then closes native V4L2 device cleanly

Why:
- this allows real Linux webcam runs to flow through existing event/metrics pipeline without shelling to OpenCV read loop when native mmap is available.

4. Added deterministic smoke tests for new frame acquisition behavior
- Updated: `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`

New test coverage:
- `TestPullFramesClassifiesTimeoutReceivedIncomplete`
  - validates timeout + received + incomplete classification in one run
  - validates frame-id progression and monotonic steady timestamps
- `TestPullFramesDqbufFailureIsActionable`
  - validates actionable error on dequeue failure

Test harness enhancements:
- fake `poll` sequence planner
- fake `VIDIOC_DQBUF` scripted results (`bytes_used`, `flags`)
- deterministic monotonic clock override via `steady_now_fn`

Why:
- protects the exact acquisition contract in CI without camera hardware.

5. Updated module docs for Linux acquisition lifecycle
- Updated:
  - `src/backends/webcam/linux/README.md`
  - `src/backends/webcam/README.md`
  - `tests/backends/README.md`

Why:
- keeps handoff docs aligned with current implementation and expected behavior.

### Files changed
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

3. Focused webcam tests
- Command:
  - `ctest --test-dir build -R "webcam_linux_v4l2_capture_device_smoke|webcam_backend_smoke|run_webcam_selector_resolution_smoke|list_devices_webcam_backend_smoke" --output-on-failure`
- Result: passed (`4/4`)

4. Full regression suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`81/81`)

### Outcome
Linux webcam backend now has a real native frame acquisition loop (`poll + DQBUF + QBUF`) with explicit timeout/incomplete classification and monotonic internal timing, fully wired into existing event/metrics/report flow with deterministic test coverage and clean shutdown behavior.
