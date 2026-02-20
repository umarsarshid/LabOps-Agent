# LabOps Summary

## Commit: feat(webcam/linux): add native V4L2 discovery path

Date: 2026-02-20

### Goal
Implement milestone `0109` by adding Linux-native webcam enumeration that scans `/dev/video*`, queries capabilities with `VIDIOC_QUERYCAP`, and maps results into `WebcamDeviceInfo`. `list-devices --backend webcam` should surface real Linux devices even when OpenCV bootstrap is also compiled.

### What was implemented

1. Added Linux V4L2 enumerator module
- Added: `src/backends/webcam/linux/v4l2_device_enumerator.hpp`
- Added: `src/backends/webcam/linux/v4l2_device_enumerator.cpp`
- New API: `EnumerateV4l2Devices(std::vector<WebcamDeviceInfo>&, std::string&)`

Behavior:
- Scans `/dev` for character devices named `video*`
- Opens each node non-blocking (`open(..., O_RDONLY | O_NONBLOCK)`)
- Calls `VIDIOC_QUERYCAP`
- Filters to capture-capable devices (`VIDEO_CAPTURE` / `VIDEO_CAPTURE_MPLANE`)
- Maps into normalized `WebcamDeviceInfo`:
  - `device_id` from node name (example `video0`)
  - `friendly_name` from `caps.card` (fallback to `device_id`)
  - optional `bus_info` from `caps.bus_info`
  - optional `capture_index` parsed from `videoN`
- Sorts deterministically by numeric index and then path

Why:
- Gives Linux users real device identity instead of index-only probing.
- Keeps selector and list output stable/repeatable between runs.

2. Wired discovery preference order for webcam backend
- Updated: `src/backends/webcam/device_selector.cpp`
- Updated: `src/backends/webcam/device_selector.hpp`

New order when fixture is not set:
- Linux: try native V4L2 enumeration first
- If no native devices found: fall back to OpenCV probe indices
- Non-Linux: existing OpenCV fallback path remains

Why:
- Ensures Linux uses the strongest source of truth first.
- Keeps cross-platform behavior working without blocking non-Linux environments.

3. Extended CLI `list-devices` contract to include webcam backend
- Updated: `src/labops/cli/router.cpp`

Changes:
- Usage/help now shows `--backend <real|webcam>`
- Option parser accepts both `real` and `webcam`
- Added webcam listing path that prints:
  - backend/status/status_reason
  - device count
  - per-device id, friendly_name, optional bus_info, optional capture_index
- Existing real backend behavior remains unchanged

Why:
- Milestone done criteria requires direct CLI visibility for webcam devices.
- Keeps operator experience symmetric with real backend listing.

4. Added integration smoke test for webcam list-devices output contract
- Added: `tests/labops/list_devices_webcam_backend_smoke.cpp`
- Updated: `CMakeLists.txt` (new smoke test target + Linux enumerator compile unit)

Test validates:
- `labops list-devices --backend webcam` succeeds
- deterministic device ordering
- expected output fields for fixture-backed devices

Why:
- Protects CLI output contract and discovery behavior from regressions.

5. Updated module READMEs for future engineers
- Updated:
  - `src/backends/webcam/README.md`
  - `src/backends/webcam/linux/README.md`
  - `src/labops/README.md`
  - `src/labops/cli/README.md`
  - `tests/labops/README.md`

Why:
- Keeps module docs aligned with implemented behavior and discovery precedence.

### Files changed
- `CMakeLists.txt`
- `src/backends/webcam/device_selector.cpp`
- `src/backends/webcam/device_selector.hpp`
- `src/backends/webcam/linux/v4l2_device_enumerator.hpp`
- `src/backends/webcam/linux/v4l2_device_enumerator.cpp`
- `src/labops/cli/router.cpp`
- `tests/labops/list_devices_webcam_backend_smoke.cpp`
- `src/backends/webcam/README.md`
- `src/backends/webcam/linux/README.md`
- `src/labops/README.md`
- `src/labops/cli/README.md`
- `tests/labops/README.md`
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
  - `ctest --test-dir build -R "list_devices_webcam_backend_smoke|list_devices_real_backend_smoke|webcam_device_selector_smoke|run_webcam_selector_resolution_smoke|list_backends_smoke" --output-on-failure`
- Result: passed (`5/5`)

4. Full regression suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`80/80`)

### Outcome
Linux webcam discovery now prefers native V4L2 capability queries and falls back to OpenCV probing only when needed. `labops list-devices --backend webcam` now exposes real Linux device identities and remains fully covered by smoke tests.
