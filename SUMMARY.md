# LabOps Summary

## Commit: explicit config-status event emission helper

Date: 2026-02-19

### Goal
Add a single explicit event helper for config-status outcomes so `CONFIG_APPLIED`, `CONFIG_UNSUPPORTED`, and `CONFIG_ADJUSTED` are emitted through one contract path, while preserving existing payload schemas and run behavior.

### Implemented

1. Added unified config-status emission API in events facade
- Updated `src/events/emitter.hpp`:
  - added `Emitter::ConfigStatusEvent` with `Kind`:
    - `kApplied`
    - `kUnsupported`
    - `kAdjusted`
  - added `EmitConfigStatus(const ConfigStatusEvent&, std::string&)`
- Updated `src/events/emitter.cpp`:
  - implemented `EmitConfigStatus(...)` with explicit switch-to-event mapping:
    - `kApplied -> CONFIG_APPLIED`
    - `kUnsupported -> CONFIG_UNSUPPORTED`
    - `kAdjusted -> CONFIG_ADJUSTED`
  - keeps payload keys exactly aligned with existing event contract.

Why:
- Before this change, config outcome payload construction was spread across multiple methods/callsites.
- One helper centralizes contract enforcement and reduces drift risk.

2. Kept legacy typed emitter methods as wrappers (backward-compatible)
- `EmitConfigApplied(...)`, `EmitConfigUnsupported(...)`, and
  `EmitConfigAdjusted(...)` now delegate to `EmitConfigStatus(...)`.

Why:
- Preserves current callsites/tests and avoids breaking external/internal usage.
- Introduces the new helper without changing behavior expectations.

3. Wired run orchestration to the explicit helper
- Updated `src/labops/cli/router.cpp`:
  - real apply path now emits unsupported and adjusted rows through
    `EmitConfigStatus(...)`.
  - config-applied emission points also route through `EmitConfigStatus(...)`.

Why:
- Ensures backend config outcome reporting uses the unified helper in real
  execution flow, not only in isolated emitter tests.
- Keeps unsupported knobs reportable in best-effort runs without changing
  pass/fail semantics.

4. Extended emitter smoke coverage for unsupported/adjusted
- Updated `tests/events/emitter_smoke.cpp`:
  - emits `CONFIG_UNSUPPORTED` and `CONFIG_ADJUSTED` via the new helper.
  - verifies payload fields remain contract-accurate.
  - updated expected event-line count and ordering assertions.

Why:
- Locks the new helper contract with explicit payload assertions.
- Prevents regression where helper emits incorrect keys or misses fields.

5. Updated module docs
- Updated `src/events/README.md` to document explicit config-status helper usage.
- Updated `tests/events/README.md` to include emitter smoke coverage details.

Why:
- Keeps design intent and test surface discoverable for future contributors.

### Files changed
- `src/events/emitter.hpp`
- `src/events/emitter.cpp`
- `src/labops/cli/router.cpp`
- `tests/events/emitter_smoke.cpp`
- `src/events/README.md`
- `tests/events/README.md`
- `SUMMARY.md`

### Verification

1. Formatting
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- `cmake --build build`
- Result: passed

3. Focused tests
- `ctest --test-dir build -R "emitter_smoke|real_apply_mode_events_smoke|run_stream_trace_smoke" --output-on-failure`
- Result: passed

4. Full suite
- `ctest --test-dir build --output-on-failure`
- Result: passed (`75/75`)

### Notes
- No event payload schema changes were introduced.
- Best-effort real-backend runs still record unsupported knobs as evidence
  (`CONFIG_UNSUPPORTED`) without failing the run, matching existing behavior.
