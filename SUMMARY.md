# LabOps Summary

## Commit: webcam backend registry + list-backends availability reasons

Date: 2026-02-19

### Goal
Register the webcam backend in backend selection plumbing and expose webcam availability with explicit reason text in `labops list-backends`.

### Implemented

1. Added dedicated webcam factory/registry surface
- New files:
  - `src/backends/webcam/webcam_factory.hpp`
  - `src/backends/webcam/webcam_factory.cpp`
- Added `WebcamBackendAvailability` contract:
  - `compiled`
  - `available`
  - `reason`
  - `platform`
- Added factory functions:
  - `GetWebcamBackendAvailability()`
  - `CreateWebcamBackend()`

Why:
- Keeps backend registration/status logic in one place (instead of scattered in CLI).
- Gives operators explicit availability reasons without digging into implementation files.

2. Wired webcam factory into build graph
- Updated `CMakeLists.txt` to compile `src/backends/webcam/webcam_factory.cpp` into `labops_backends`.

Why:
- Ensures backend registry logic is always compiled and testable across platforms.

3. Registered webcam in CLI backend selection and status output
- Updated `src/labops/cli/router.cpp`:
  - added backend id constant: `webcam`
  - `CommandListBackends` now prints webcam availability:
    - `webcam ✅ enabled`
    - or `webcam ⚠️ disabled (<reason>)`
  - run-plan backend validation now allows: `sim`, `webcam`, `real_stub`
  - backend construction path now handles webcam via `CreateWebcamBackend()`
  - when webcam is not compiled for current target, returns actionable error:
    `webcam backend not compiled on this platform`

Why:
- Makes webcam a first-class selectable backend id.
- Keeps list-backends output immediately useful for environment diagnosis.

4. Updated scenario backend validator to include webcam
- Updated `src/scenarios/validator.cpp` backend enum check:
  - from `sim, real_stub`
  - to `sim, webcam, real_stub`

Why:
- Prevents drift between accepted runtime backends and schema validation contract.

5. Updated tests for new list-backends contract
- Updated `tests/labops/list_backends_smoke.cpp`:
  - now asserts webcam row exists
  - verifies enabled/disabled webcam state with reason text from factory status
- Existing smoke tests continue validating real backend state variants.

Why:
- Locks CLI output contract for webcam status so future changes cannot silently regress operator messaging.

6. Updated docs/readmes for discoverability
- Updated:
  - `src/backends/README.md`
  - `src/backends/webcam/README.md`
  - `src/labops/cli/README.md`
  - `tests/labops/README.md`

Why:
- Keeps architecture and test expectations explicit for future contributors.

### Files changed
- `CMakeLists.txt`
- `src/backends/README.md`
- `src/backends/webcam/README.md`
- `src/backends/webcam/webcam_factory.hpp` (new)
- `src/backends/webcam/webcam_factory.cpp` (new)
- `src/backends/webcam/linux/platform_probe_linux.cpp` (format)
- `src/backends/webcam/macos/platform_probe_macos.cpp` (format)
- `src/backends/webcam/windows/platform_probe_windows.cpp` (format)
- `src/backends/webcam/webcam_backend.cpp` (format)
- `src/labops/cli/router.cpp`
- `src/labops/cli/README.md`
- `src/scenarios/validator.cpp`
- `tests/labops/list_backends_smoke.cpp`
- `tests/labops/README.md`
- `SUMMARY.md`

### Verification

1. Formatting
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- `cmake --build build`
- Result: passed

3. Focused tests
- `ctest --test-dir build -R "list_backends_smoke|scenario_validation_smoke|webcam_backend_smoke|run_stream_trace_smoke" --output-on-failure`
- Result: passed

4. Full suite
- `ctest --test-dir build --output-on-failure`
- Result: passed (`74/74`)

### Notes
- Webcam backend remains intentionally unavailable at runtime until platform capture loops are implemented.
- This commit focuses on registration and visibility contracts, not capture functionality.
