# feat(cli): harden camera run lifecycle

## Why this change
A camera can stay open when a run is interrupted or when another process already has the device. That creates confusing follow-up failures (`device busy`, missing artifacts, wedged sessions). This change improves safety and operator guidance without changing output contracts.

## What was implemented

### 1) Multi-signal interruption handling in run flow
Files:
- `src/labops/cli/router.cpp`

Changes:
- Extended scoped signal handling from only `SIGINT` to:
  - `SIGINT`
  - `SIGTERM`
  - `SIGHUP` (when available)
- Recorded the triggering signal number in shared run state.
- Updated interrupt messaging/logging to report the specific signal name (`SIGINT` / `SIGTERM` / `SIGHUP`).

Why:
- Runs now clean up consistently when stopped by terminal close, service stop, or normal Ctrl+C.

### 2) Single-run lock for camera-oriented runs
Files:
- `src/labops/cli/router.cpp`
- `tests/labops/run_single_process_lock_smoke.cpp` (new)
- `CMakeLists.txt`

Changes:
- Added `ScopedSingleRunLock` with lock file at `tmp/labops.lock`.
- Lock behavior:
  - detects active PID lock and fails fast with actionable message,
  - removes stale lock files safely,
  - writes current PID on acquire,
  - auto-releases lock on teardown.
- Enabled lock acquisition in run preparation for camera backends (`webcam`, and real-enabled stub path).
- Added smoke test `run_single_process_lock_smoke` verifying fail-fast lock enforcement.
- Registered new smoke test target in CMake.

Why:
- Prevents accidental concurrent runs that can leave camera devices busy and hard to diagnose.

### 3) Better webcam recovery hints on connect/selector failures
Files:
- `src/labops/cli/router.cpp`

Changes:
- Added string-token based recovery hint generator for common webcam failure text (permission/busy/discovery patterns).
- Emits platform-aware quick help, e.g.:
  - macOS: `lsof -nP | rg -i "camera|avfoundation|coremediaio|labops"`
  - Linux: `lsof /dev/video*`
- Hooked hints into webcam selector resolution/connect/start failure paths.

Why:
- Engineers get immediate next steps instead of generic failure output.

### 4) Documentation updates
Files:
- `src/labops/cli/README.md`
- `tests/labops/README.md`

Changes:
- Documented new lock behavior (`tmp/labops.lock`) for camera runs.
- Updated interrupt docs to include all supported stop signals.
- Added smoke test entry for run-lock contract.

Why:
- Keeps ops and test documentation aligned with runtime behavior.

### 5) Formatting-only collateral update
Files:
- `tests/backends/webcam_linux_mock_provider_smoke.cpp`

Changes:
- `clang-format` normalization only.

Why:
- Keep style gate green and consistent.

## Verification performed

1. Formatting
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- `cmake --build build`
- Result: passed

3. Full regression suite
- `ctest --test-dir build --output-on-failure`
- Result: passed (`83/83`)

## Outcome
Camera run lifecycle is safer and easier to operate:
- interruptions flush and clean up under more real-world stop paths,
- duplicate concurrent camera runs are blocked deterministically,
- webcam failure output now includes practical recovery commands.
