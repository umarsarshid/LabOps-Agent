# 0116 — Deterministic Linux Webcam Mock-Provider Test Path

## Goal
Add a Linux-native deterministic test path for webcam V4L2 behavior that does not require `/dev/video*` access or real camera hardware.

## What Was Implemented

### 1) Added a dedicated Linux fake device smoke test
Files:
- `tests/backends/webcam_linux_mock_provider_smoke.cpp`

Change:
- Added a new Linux-only smoke test binary centered on a test harness class:
  - `FakeV4l2Device`
- `FakeV4l2Device` injects scripted behavior via `V4l2CaptureDevice::IoOps` and never touches kernel devices.

What the fake harness models:
- `VIDIOC_QUERYCAP`, `VIDIOC_G_FMT`, `VIDIOC_S_FMT`
- `VIDIOC_G_PARM`, `VIDIOC_S_PARM`
- mmap stream bootstrap ioctls (`REQBUFS`, `QUERYBUF`, `QBUF`, `STREAMON`, `STREAMOFF`)
- streaming dequeue path (`poll`, `DQBUF`, `QBUF`)
- monotonic capture time progression through injected steady clock

Why:
- This creates a fully deterministic Linux-native validation path for V4L2 behavior in CI/dev environments without real camera hardware.
- It prevents future Linux path regressions from being masked by machine/hardware differences.

### 2) Added required behavioral coverage
Files:
- `tests/backends/webcam_linux_mock_provider_smoke.cpp`

New test cases:
- `TestTimeoutSequenceClassification`
  - scripted `poll` timeouts + one ready dequeue
  - verifies emitted sequence `TIMEOUT -> TIMEOUT -> RECEIVED`
  - verifies frame-id progression
- `TestIncompleteBufferClassification`
  - scripted ready dequeue with `bytesused=0` + `V4L2_BUF_FLAG_ERROR`
  - verifies `INCOMPLETE` classification
- `TestAdjustedFormatBehavior`
  - scripted driver adjustments for width/height/pixel-format/fps
  - verifies `ApplyRequestedFormatBestEffort` produces adjusted readback rows

Why:
- These are exactly the Linux behaviors engineers care about for triage quality:
  timeout cadence, incomplete payload detection, and requested-vs-actual config drift.

### 3) Wired test into build/test system
Files:
- `CMakeLists.txt`

Change:
- Added new smoke target:
  - `webcam_linux_mock_provider_smoke`

Why:
- Makes deterministic Linux mock-provider coverage part of the normal `ctest` surface.

### 4) Updated backend test documentation
Files:
- `tests/backends/README.md`

Change:
- Documented `webcam_linux_mock_provider_smoke.cpp` purpose and coverage.

Why:
- Keeps test intent visible for future contributors and avoids duplicated ad-hoc mocks.

## Verification Performed

1. Formatting:
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build:
- `cmake --build build`
- Result: passed

3. Focused coverage for webcam mock paths:
- `ctest --test-dir build -R "webcam_linux_mock_provider_smoke|webcam_linux_v4l2_capture_device_smoke|webcam_opencv_mock_provider_smoke" --output-on-failure`
- Result: passed (`3/3`)

4. Full regression:
- `ctest --test-dir build --output-on-failure`
- Result: passed (`82/82`)

## Outcome
Linux-native webcam logic now has an explicit deterministic mock-provider test path, covering timeout sequences, incomplete frame classification, and adjusted format behavior without requiring real devices.
