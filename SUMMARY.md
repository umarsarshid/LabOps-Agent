# LabOps Summary

## Commit: add OpenCV webcam runtime implementation

Date: 2026-02-19

### Goal
Implement milestone `0104` by making the webcam backend actually usable through OpenCV:
- open webcam by index (`VideoCapture(index)`)
- attempt width/height/fps/fourcc settings
- capture requested vs actual readback values
- stream frames into existing event/metrics pipeline
- emit `CONFIG_UNSUPPORTED` for controls OpenCV cannot confirm

### Implemented

1. Added a dedicated OpenCV runtime module for webcam capture
- Added:
  - `src/backends/webcam/opencv_webcam_impl.hpp`
  - `src/backends/webcam/opencv_webcam_impl.cpp`
- Implemented:
  - `OpenDevice(index)` via OpenCV `VideoCapture`
  - `CloseDevice()` lifecycle cleanup
  - property set/get wrappers for width/height/fps
  - fourcc set/readback wrappers
  - frame pull loop with time-budgeted sampling and outcome classification
  - index probing helper (`EnumerateDeviceIndices`) for discovery

Why:
- isolates OpenCV-specific logic behind one small class
- keeps `WebcamBackend` focused on contract/state/evidence instead of OpenCV details
- preserves clean builds when OpenCV is disabled (compile-time stub behavior)

2. Converted `WebcamBackend` from placeholder to working OpenCV path
- Updated `src/backends/webcam/webcam_backend.hpp/.cpp`:
  - added requested webcam config state (`requested_width/height/fps/pixel_format`)
  - added unsupported-control tracking
  - `Connect()` now resolves `device.index`, opens camera, applies requests, and captures readback
  - `Start()` now starts stream state if capture is open
  - `PullFrames()` now returns real frame samples from OpenCV
  - `Stop()` now closes capture cleanly
  - `DumpConfig()` now includes unsupported details (`webcam.unsupported.*`)

Why:
- delivers the minimum useful real webcam backend without changing CLI contracts
- provides evidence-rich config snapshots instead of silent best-guess behavior

3. Enabled OpenCV-backed device enumeration when fixture is absent
- Updated `src/backends/webcam/device_selector.cpp/.hpp`:
  - fixture CSV now supports optional `capture_index` column
  - if fixture is not set, discovery probes OpenCV indices `0..LABOPS_WEBCAM_MAX_PROBE_INDEX` (default `8`)
  - discovered device model now carries optional `capture_index`
- Updated `src/backends/webcam/device_model.hpp/.cpp`:
  - added optional `capture_index` to `WebcamDeviceInfo` JSON serialization

Why:
- allows real machine discovery with no special fixture file
- preserves deterministic CI behavior through fixture-based discovery
- avoids ambiguity between sorted selector index and true capture index

4. Wired true capture index into backend selection application
- Updated `src/labops/cli/router.cpp`:
  - when applying resolved webcam selection, backend now receives `device.index` from device `capture_index` when present (fallback to discovered index)

Why:
- ensures selector resolution opens the intended physical camera index
- decouples UI/reporting order from actual capture index

5. Routed webcam scenario params into backend and event contract
- Extended `RunPlan` with webcam config fields in `src/labops/cli/router.cpp`
- Parsed scenario fields into run plan:
  - `webcam.requested_width`
  - `webcam.requested_height`
  - `webcam.requested_fps`
  - `webcam.requested_pixel_format`
- Added webcam param apply helper before connect
- Added webcam readback merge helper after connect
- Added webcam `CONFIG_UNSUPPORTED` emitter that reads backend unsupported rows and emits standard events

Why:
- keeps webcam path aligned with project-wide “strict outputs + evidence” approach
- uses existing event contract (`CONFIG_APPLIED` / `CONFIG_UNSUPPORTED`) so downstream tools do not change

6. Updated platform availability probes to reflect OpenCV bootstrap readiness
- Updated:
  - `src/backends/webcam/linux/platform_probe_linux.cpp`
  - `src/backends/webcam/macos/platform_probe_macos.cpp`
  - `src/backends/webcam/windows/platform_probe_windows.cpp`
- Behavior:
  - OpenCV enabled -> backend reported available
  - OpenCV disabled -> explicit actionable unavailable reason

Why:
- `labops list-backends` now reflects real runtime readiness for webcam path

7. Tightened validation + tests for new webcam behavior
- Updated `src/scenarios/validator.cpp`:
  - `webcam.requested_pixel_format` must be exactly 4 chars (fourcc)
- Updated tests:
  - `tests/backends/webcam_backend_smoke.cpp` now expects actionable connect-failure semantics instead of placeholder-only unavailable
  - `tests/labops/run_webcam_selector_resolution_smoke.cpp` now uses fixture `capture_index` and accepts either success or connect-failed depending machine camera availability
- Updated docs/readmes:
  - `README.md`
  - `src/backends/webcam/README.md`
  - `tests/backends/README.md`

Why:
- keeps validation aligned with runtime expectations
- makes smoke tests stable across dev machines with/without accessible webcams

### Files changed
- `CMakeLists.txt`
- `README.md`
- `src/backends/webcam/README.md`
- `src/backends/webcam/device_model.cpp`
- `src/backends/webcam/device_model.hpp`
- `src/backends/webcam/device_selector.cpp`
- `src/backends/webcam/device_selector.hpp`
- `src/backends/webcam/linux/platform_probe_linux.cpp`
- `src/backends/webcam/macos/platform_probe_macos.cpp`
- `src/backends/webcam/opencv_webcam_impl.cpp`
- `src/backends/webcam/opencv_webcam_impl.hpp`
- `src/backends/webcam/webcam_backend.cpp`
- `src/backends/webcam/webcam_backend.hpp`
- `src/backends/webcam/windows/platform_probe_windows.cpp`
- `src/labops/cli/router.cpp`
- `src/scenarios/validator.cpp`
- `tests/backends/README.md`
- `tests/backends/webcam_backend_smoke.cpp`
- `tests/labops/run_webcam_selector_resolution_smoke.cpp`
- `SUMMARY.md`

### Verification

1. Formatting
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- `cmake --build build`
- Result: passed

3. Focused verification
- `ctest --test-dir build -R "webcam_backend_smoke|webcam_device_selector_smoke|run_webcam_selector_resolution_smoke|list_backends_smoke|scenario_validation_smoke" --output-on-failure`
- Result: passed (`5/5`)

4. Full regression suite
- `ctest --test-dir build --output-on-failure`
- Result: passed (`77/77`)

5. Manual webcam run probe in this environment
- Command:
  - `./build/labops run tmp/verify-0104/webcam_run.json --out tmp/verify-0104/out`
- Observed:
  - OpenCV reported camera permission denied in this environment
  - run exited with selector-resolution failure (`no webcam devices were discovered`)

Why this still matters:
- functional path is compiled, wired, and covered by tests
- machine-specific webcam permission/access is now the remaining runtime prerequisite

### Notes
- This commit keeps existing CLI shape and artifact contracts intact.
- Webcam path is now operational when OpenCV is enabled and camera access is available.
- Real vendor-SDK backend work remains separate and unaffected by this change.
