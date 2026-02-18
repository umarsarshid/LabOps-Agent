# Commit Summary

## Commit
`feat(real): add reconnect policy for mid-stream disconnect handling`

## What I Implemented

This commit adds a real-backend reconnect policy in the CLI run pipeline so a
mid-stream disconnect no longer hard-crashes the run path.

In plain language:
- when frame pull detects a disconnect, LabOps emits `DEVICE_DISCONNECTED`
- LabOps retries reconnect with a bounded budget (3 total attempts)
- if reconnect still fails, LabOps exits non-zero **but still writes a complete
  failure bundle** (`run.json`, `events.jsonl`, `metrics.*`, `summary.md`,
  `report.html`, `bundle_manifest.json`)

## Why This Was Needed

Hardware runs can lose connection mid-test (unplug, cable flap, NIC reset,
device reboot). The old behavior treated this like a generic fatal error and
returned early before full triage artifacts were guaranteed.

HW/SW teams need:
- explicit disconnect evidence in timeline events
- deterministic reconnect behavior (not infinite retries)
- complete failure packets for debugging and handoff

## Detailed Changes

### 1) Added a dedicated disconnect event type
Files:
- `src/events/event_model.hpp`
- `src/events/event_model.cpp`

What changed:
- Added `EventType::kDeviceDisconnected`
- Added JSON mapping to `"DEVICE_DISCONNECTED"`

Why:
- disconnects are a first-class triage signal and should not be inferred from
  generic error text.

### 2) Strengthened OSS real-backend disconnect fixture behavior
Files:
- `src/backends/real_sdk/real_backend.hpp`
- `src/backends/real_sdk/real_backend.cpp`

What changed:
- Added fixture env handling for
  `LABOPS_REAL_DISCONNECT_AFTER_PULLS` (existing in-progress work completed)
- Once disconnect is triggered, backend now latches an unavailable state:
  subsequent `Connect()` fails with `device unavailable after disconnect`
- `PullFrames()` still emits deterministic `device disconnected during acquisition`
- `Stop()` remains idempotent for cleanup/finalization paths

Why:
- this gives deterministic retry-exhaustion behavior in OSS CI without needing
  physical unplug tests.

### 3) Implemented reconnect policy in run pipeline
File:
- `src/labops/cli/router.cpp`

What changed:
- Added disconnect detection helper (`IsLikelyDisconnectError`)
- Added reconnect helper (`TryReconnectAfterDisconnect`)
- In non-soak real run loop:
  - on disconnect error:
    - emit `DEVICE_DISCONNECTED` event
    - retry reconnect/start within a bounded budget
    - continue run if reconnect succeeds
    - if budget is exhausted, stop acquisition loop and finalize artifacts
- Added final run-state handling for disconnect failure:
  - `STREAM_STOPPED` reason set to `device_disconnect`
  - payload includes reconnect budget/attempt details
  - metrics duration uses completed partial duration
  - summary threshold failures include disconnect exhaustion reason
  - CLI exits non-zero with explicit reconnect exhaustion message

Why:
- satisfies the milestone requirement directly:
  `DEVICE_DISCONNECTED` + retry N + clean failure packet.

### 4) Added integration coverage for reconnect policy
Files:
- `tests/labops/run_reconnect_policy_smoke.cpp` (new)
- `CMakeLists.txt`
- `tests/labops/README.md`

What changed:
- New CLI smoke test validates reconnect-policy contract (real-enabled builds):
  - deterministic forced disconnect
  - non-zero exit after reconnect exhaustion
  - required failure artifacts still present
  - `DEVICE_DISCONNECTED` + `STREAM_STOPPED reason=device_disconnect` present
  - summary includes disconnect exhaustion explanation
- Added test target wiring in CMake

Why:
- prevents regressions in the exact behavior operators need during real failures.

### 5) Updated event/triage docs and smoke coverage
Files:
- `tests/core/event_json_test.cpp`
- `tests/events/events_jsonl_smoke.cpp`
- `tests/events/README.md`
- `src/events/README.md`
- `src/labops/README.md`
- `src/backends/real_sdk/README.md`
- `docs/triage_bundle_spec.md`

What changed:
- Event mapping tests now include `DEVICE_DISCONNECTED`
- JSONL smoke now validates `DEVICE_DISCONNECTED` serialization in output
- Docs updated to describe disconnect event and reconnect-failure finalization

Why:
- keeps implementation contract and operator documentation aligned.

## Verification Performed

### Formatting
- `bash tools/clang_format.sh --fix`
- `bash tools/clang_format.sh --check`
- Result: pass

### Build
- `cmake --build build`
- Result: pass

### Focused tests
- `ctest --test-dir build -R "events_jsonl_smoke|run_reconnect_policy_smoke|run_interrupt_flush_smoke|run_stream_trace_smoke|run_backend_connect_failure_smoke" --output-on-failure`
- Result: pass (`5/5`)

### Full suite
- `ctest --test-dir build --output-on-failure`
- Result: pass (`62/62`)

## Notes About This Environment

- Build output reports `SDK missing (real backend disabled)` in this workspace.
- The reconnect smoke test is written to pass in both states:
  - real backend enabled -> full reconnect assertions execute
  - real backend disabled -> test exits early (no false failures)

## Outcome

LabOps now handles mid-stream real-backend disconnects in an
engineering-friendly way: explicit disconnect events, bounded reconnect policy,
and a complete failure bundle when retries are exhausted.
