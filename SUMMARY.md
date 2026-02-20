# LabOps Summary

## Commit: feat(webcam/linux): apply requested format with readback evidence

Date: 2026-02-20

### Goal
Implement milestone `0112` for Linux webcam runs:
- Apply requested `width/height/pixel_format` via `VIDIOC_S_FMT`
- Apply requested `fps` via `VIDIOC_S_PARM` when supported
- Keep behavior best-effort (do not hard-fail unsupported controls)
- Emit explicit unsupported/adjusted evidence
- Record requested vs actual values so artifacts reflect what the driver really accepted

Done criteria:
- `CONFIG_UNSUPPORTED` is emitted when a control cannot be applied/supported
- `CONFIG_ADJUSTED` is emitted when the driver negotiates to a different value
- `camera_config.json` and `config_report.md` include stable requested vs actual evidence for webcam controls

### What was implemented

1. Added Linux native V4L2 best-effort format apply/readback API
- Updated: `src/backends/webcam/linux/v4l2_capture_device.hpp`
- Updated: `src/backends/webcam/linux/v4l2_capture_device.cpp`

New structs:
- `V4l2RequestedFormat`
- `V4l2AppliedControl`
- `V4l2ApplyResult`

New method:
- `ApplyRequestedFormatBestEffort(...)`

Behavior:
- Applies width/height/pixel format through `VIDIOC_S_FMT`
- Applies fps through `VIDIOC_G_PARM`/`VIDIOC_S_PARM`/readback `VIDIOC_G_PARM`
- Produces one per-control row with:
  - `supported`
  - `applied`
  - `adjusted`
  - `requested_value`
  - `actual_value`
  - actionable `reason`
- Treats unsupported/rejected controls as per-control unsupported rows (best-effort)
- Preserves hard failure only for true contract misuse (e.g., device not open)

Why:
- Engineers need negotiated readback, not only requested intent.
- Drivers often adjust camera values; recording this removes false assumptions and reduces flaky threshold debugging.

2. Upgraded webcam backend evidence model to include unsupported + adjusted + readback rows
- Updated: `src/backends/webcam/webcam_backend.hpp`
- Updated: `src/backends/webcam/webcam_backend.cpp`

Changes:
- Added backend-side row models:
  - `AdjustedControl`
  - `ReadbackRow`
- Added state collections:
  - `adjusted_controls_`
  - `readback_rows_`
  - `linux_native_config_applied_`
- Added Linux-native apply integration:
  - `ApplyLinuxRequestedConfigBestEffort(...)`
- `Connect(...)` now:
  - clears previous session config snapshot
  - probes and applies Linux-native requested controls best-effort
  - keeps OpenCV fallback behavior intact
- `ApplyRequestedConfig(...)` now:
  - short-circuits if Linux native apply already succeeded
  - records readback/adjusted rows for OpenCV path too

Dump config contract additions:
- `webcam.native_apply_used`
- `webcam.readback.count`
- `webcam.readback.<i>.*`
- `webcam.adjusted.count`
- `webcam.adjusted.<i>.*`
- existing `webcam.unsupported.*` retained

Why:
- Router and artifact layers need normalized evidence regardless of whether the applied path was V4L2-native or OpenCV fallback.

3. Wired webcam config status events + config artifacts into run orchestration
- Updated: `src/labops/cli/router.cpp`

Added helpers:
- `BuildWebcamApplyEvidence(...)`
  - parses backend dump rows into typed apply evidence
- `EmitWebcamConfigStatusEvents(...)`
  - emits `CONFIG_UNSUPPORTED` and `CONFIG_ADJUSTED` from webcam apply evidence
- `WriteWebcamConfigArtifacts(...)`
  - writes:
    - `config_verify.json`
    - `camera_config.json`
    - `config_report.md`

Run flow change:
- Webcam backend path now builds typed evidence and writes the same config artifacts/report style as real backend flow.

Why:
- This makes webcam runs produce engineer-usable config diagnostics with the same operational model already used by real backend triage.

4. Expanded artifact curation/report notes for webcam controls
- Updated: `src/artifacts/camera_config_writer.cpp`
- Updated: `src/artifacts/config_report_writer.cpp`

Changes:
- Added curated generic keys:
  - `width`, `height`, `fps`
- Added report unit/negotiation notes:
  - width/height via `VIDIOC_S_FMT`
  - fps via `VIDIOC_S_PARM` when supported

Why:
- Keeps report readability high and avoids forcing engineers to decode low-level backend fields.

5. Added deterministic Linux V4L2 apply/readback smoke coverage
- Updated: `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`

New test coverage:
- `TestApplyRequestedFormatCapturesAdjustedReadback`
  - verifies adjusted rows for width/height/pixel format/fps
- `TestApplyRequestedFormatCapturesUnsupported`
  - verifies unsupported rows when `S_FMT` fails or `TIMEPERFRAME` unsupported

Also extended fake ioctl harness to model:
- `G_FMT/S_FMT`
- `G_PARM/S_PARM`
- adjustable negotiated values
- unsupported/failure toggles

Why:
- Protects the exact best-effort/adjusted semantics in CI without hardware dependency.

### Files changed
- `src/backends/webcam/linux/v4l2_capture_device.hpp`
- `src/backends/webcam/linux/v4l2_capture_device.cpp`
- `src/backends/webcam/webcam_backend.hpp`
- `src/backends/webcam/webcam_backend.cpp`
- `src/labops/cli/router.cpp`
- `src/artifacts/camera_config_writer.cpp`
- `src/artifacts/config_report_writer.cpp`
- `tests/backends/webcam_linux_v4l2_capture_device_smoke.cpp`
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
  - `ctest --test-dir build -R "webcam_linux_v4l2_capture_device_smoke|webcam_backend_smoke|run_webcam_selector_resolution_smoke|list_devices_webcam_backend_smoke|real_apply_mode_events_smoke" --output-on-failure`
- Result: passed (`5/5`)

4. Full suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`81/81`)

### Outcome
Linux webcam runs now apply requested format/fps best-effort, emit explicit unsupported/adjusted evidence events, and persist requested-vs-actual config diagnostics in standard triage artifacts (`config_verify.json`, `camera_config.json`, `config_report.md`).
