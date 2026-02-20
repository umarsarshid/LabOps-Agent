# LabOps Summary

## Commit: feat(webcam/linux): model V4L2 supported controls

Date: 2026-02-20

### Goal
Implement milestone `0110` by populating Linux V4L2 `supported_controls` for webcam devices and exposing that support clearly in device reports.

Target support coverage:
- width/height from V4L2 format/size enumeration
- pixel formats from `VIDIOC_ENUM_FMT`
- fps list/range best-effort from `VIDIOC_ENUM_FRAMEINTERVALS`
- exposure/gain/auto-exposure best-effort from `VIDIOC_QUERYCTRL`

### What was implemented

1. Expanded Linux V4L2 discovery to build supported-controls snapshot
- Updated: `src/backends/webcam/linux/v4l2_device_enumerator.cpp`
- Updated: `src/backends/webcam/linux/v4l2_device_enumerator.hpp`

Added internal discovery pipeline:
- robust ioctl wrapper (`IoctlRetry`) to tolerate `EINTR`
- pixel-format discovery:
  - `VIDIOC_ENUM_FMT` per capture type
  - fourcc decode to readable strings
- width/height discovery:
  - `VIDIOC_ENUM_FRAMESIZES`
  - supports discrete and stepwise/continuous sizing
  - records min/max and step (when available)
- fps discovery (best-effort):
  - `VIDIOC_ENUM_FRAMEINTERVALS`
  - records discrete fps values where available
  - records fps min/max for stepwise/continuous intervals
- control discovery (best-effort):
  - `VIDIOC_QUERYCTRL` for `EXPOSURE_ABSOLUTE`, `GAIN`, `EXPOSURE_AUTO`
  - `VIDIOC_QUERYMENU` for menu-based controls
  - maps to normalized `WebcamControlSpec` with:
    - `value_type`
    - range hints
    - enum values
    - `read_only`

Why:
- This turns Linux discovery from “device identity only” into “identity + capability evidence”.
- Engineers can now see upfront what knobs are actually available before attempting a run.

2. Added explicit webcam supported-control reporting in CLI device report
- Updated: `src/labops/cli/router.cpp`

Added helper output path:
- `device[i].supported_controls.count: N`
- per supported control:
  - `id`
  - `value_type`
  - `range_min`, `range_max`, `range_step` (when present)
  - `enum_values` (when present)
  - `read_only`

Why:
- Milestone “done when” asks for explicit support listing in the device report.
- This keeps report output human-readable and automation-friendly.

3. Strengthened webcam list-devices smoke contract
- Updated: `tests/labops/list_devices_webcam_backend_smoke.cpp`

New assertions:
- each fixture-discovered device explicitly prints:
  - `supported_controls.count: 0`

Why:
- Guarantees the device report always includes explicit support visibility, even when controls are absent.

4. Updated module docs for handoff clarity
- Updated:
  - `src/backends/webcam/README.md`
  - `src/backends/webcam/linux/README.md`
  - `src/labops/README.md`
  - `src/labops/cli/README.md`
  - `tests/labops/README.md`

Why:
- Keeps behavior/docs aligned so future engineers do not need code archeology.

### Files changed
- `src/backends/webcam/linux/v4l2_device_enumerator.cpp`
- `src/backends/webcam/linux/v4l2_device_enumerator.hpp`
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
  - `ctest --test-dir build -R "list_devices_webcam_backend_smoke|webcam_device_model_smoke|webcam_device_selector_smoke|run_webcam_selector_resolution_smoke|list_backends_smoke" --output-on-failure`
- Result: passed (`5/5`)

4. Full regression suite
- Command: `ctest --test-dir build --output-on-failure`
- Result: passed (`80/80`)

### Outcome
Linux webcam discovery now captures and exposes supported-control evidence in a normalized form, and `labops list-devices --backend webcam` explicitly reports what each device supports.
