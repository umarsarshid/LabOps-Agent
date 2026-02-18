# Commit Summary

## Commit
`feat(real): add optional SDK log capture to run bundles`

## What I Implemented

This commit adds optional SDK/backend log capture controlled by a new CLI flag:

- `--sdk-log`

When enabled for real-backend runs (`backend: real_stub` path), LabOps now
writes:

- `sdk_log.txt`

into the run bundle directory.

In plain language:
- normal runs are unchanged by default
- if you opt in with `--sdk-log`, you get an extra backend-level log file for
  low-level troubleshooting

## Why This Was Needed

HW/SW teams often need extra context from vendor/backend integration layers
when diagnosing tricky failures (connect/setup/session transitions).

Before this commit, bundles had rich run/events/metrics evidence but no direct
SDK/backend diagnostic log stream.

This change adds that capability while preserving default behavior and runtime
cost for standard runs.

## Detailed Changes

### 1) Added CLI contract for optional SDK log capture
Files:
- `src/labops/cli/router.hpp`
- `src/labops/cli/router.cpp`

What changed:
- Added `RunOptions::capture_sdk_log` (default `false`)
- Added `--sdk-log` parsing for:
  - `labops run ...`
  - `labops baseline capture ...`
- Updated usage/help text to include `--sdk-log`

Why:
- makes capture explicit/opt-in so normal runs remain unaffected.

### 2) Wired `--sdk-log` into run pipeline
File:
- `src/labops/cli/router.cpp`

What changed:
- Added `ConfigureOptionalSdkLogCapture(...)` helper with clear behavior:
  - if flag disabled: no-op
  - if non-real backend: warn and ignore
  - if real backend path: sets backend param `sdk.log.path` to
    `<bundle_dir>/sdk_log.txt`
- Added `sdk_log_artifact_path` tracking through run lifecycle
- Included `sdk_log.txt` in optional manifest artifact assembly for:
  - soak pause manifests
  - final completion manifests
- Added run output/log visibility:
  - request log now includes `sdk_log=true|false`
  - CLI output includes `sdk_log_capture: disabled|ignored|enabled`
  - prints `sdk_log: <path>` when available
  - backend-connect-failure path now prints sdk log artifact info when present

Why:
- ensures optional log capture participates cleanly in existing bundle and
  manifest contracts.

### 3) Implemented actual sdk log writing in real backend
Files:
- `src/backends/real_sdk/real_backend.hpp`
- `src/backends/real_sdk/real_backend.cpp`

What changed:
- Added backend-side log sink path (`sdk_log_path_`) and helper
  `AppendSdkLog(...)`
- Added special handling for `SetParam("sdk.log.path", ...)`:
  - creates/truncates `sdk_log.txt`
  - writes bootstrap header line
- Added best-effort log lines across lifecycle and pull flow:
  - `connect`, `start`, `stop`
  - parameter acceptance events
  - pull errors and successes
  - disconnect fixture trigger

Why:
- captures backend lifecycle details directly where they occur, without changing
  external run behavior.

### 4) Implemented sdk log writing in sdk stub fallback
Files:
- `src/backends/sdk_stub/real_camera_backend_stub.hpp`
- `src/backends/sdk_stub/real_camera_backend_stub.cpp`

What changed:
- Added same `sdk_log_path_` + append helper pattern for stub path
- Added `SetParam("sdk.log.path", ...)` handling to create `sdk_log.txt`
- Added log line emission for stub connect/start/stop/pull/set_param outcomes

Why:
- in OSS/CI environments where proprietary SDK is unavailable, real-backend
  flows still get useful `sdk_log.txt` evidence when requested.

### 5) Added integration smoke test for the new behavior
Files:
- `tests/labops/sdk_log_capture_smoke.cpp` (new)
- `CMakeLists.txt`
- `tests/labops/README.md`

What changed:
- New CLI smoke verifies:
  - run without `--sdk-log` does **not** produce `sdk_log.txt`
  - run with `--sdk-log` **does** produce `sdk_log.txt`
  - enabling `--sdk-log` does not change run exit behavior
- Added CMake test target wiring

Why:
- locks in the contract that capture is optional and non-invasive.

### 6) Updated module and handoff documentation
Files:
- `src/labops/README.md`
- `docs/triage_bundle_spec.md`
- `src/backends/real_sdk/README.md`
- `src/backends/sdk_stub/README.md`
- `AGENTS.md`

What changed:
- documented `--sdk-log` option and `sdk_log.txt` bundle behavior
- documented conditional artifact status in triage bundle spec
- updated handoff snapshot and capabilities list

Why:
- keeps implementation, user expectations, and handoff context aligned.

## Verification Performed

### Formatting
- `bash tools/clang_format.sh --check`
- Result: pass

### Build
- `cmake --build build`
- Result: pass

### Focused tests
- `ctest --test-dir build -R "sdk_log_capture_smoke|run_reconnect_policy_smoke|run_backend_connect_failure_smoke|run_stream_trace_smoke" --output-on-failure`
- Result: pass (`4/4`)

### Full suite
- `ctest --test-dir build --output-on-failure`
- Result: pass (`63/63`)

## Outcome

LabOps can now optionally capture backend/SDK-level diagnostic logs into
`sdk_log.txt` inside run bundles, without changing default run behavior.
