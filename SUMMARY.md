# LabOps Summary

## Commit: architecture invariants doc + contract smoke checks

Date: 2026-02-19

### Goal
Make architecture-level behavior contracts explicit and enforceable with a lightweight CI check, without changing runtime behavior.

### Implemented

1. Added architecture invariants documentation
- New file: `docs/architecture_invariants.md`
- Captures stable, cross-cutting contracts:
  - bundle artifact names/layout
  - exit-code contract
  - event stream structure and required lifecycle events
  - threshold pass/fail semantics
  - `run.json` / `metrics.json` structural field expectations
- Includes a change policy so future contract updates are deliberate and paired with tests/docs.

Why:
- These invariants were previously implicit and spread across many tests/files.
- A single architecture contract doc reduces accidental behavior drift during refactors.

2. Added lightweight contract-check smoke test
- New file: `tests/labops/architecture_contract_check_smoke.cpp`
- New CMake target: `architecture_contract_check_smoke`
- What it validates from real command execution (`labops::cli::Dispatch`):
  - passing run returns `ExitCode::kSuccess`
  - required core artifacts are emitted in run bundle
  - `run.json` contains stable run/config/timestamp fields
  - `metrics.json` contains core metrics keys
  - `events.jsonl` contains `STREAM_STARTED` and `STREAM_STOPPED`
  - event payloads include key contract fields (`run_id`, `scenario_id`, frame counters)
  - threshold-fail scenario returns `ExitCode::kThresholdsFailed` and still emits evidence with summary `FAIL`
  - invalid scenario validation returns `ExitCode::kSchemaInvalid`

Why:
- Adds one high-signal “contract guardrail” test spanning the most important architecture invariants.
- Prevents subtle contract breaks that may not be caught by module-level tests alone.

3. Documentation index updates
- Updated `docs/README.md` to include `architecture_invariants.md`.
- Updated `tests/labops/README.md` with the new contract smoke test description.

Why:
- Keeps docs and test inventory discoverable for future contributors.

4. Required formatting cleanup for existing tracked test file
- `tests/labops/soak_checkpoint_resilience_smoke.cpp` had clang-format violations under current formatter rules.
- Applied formatting-only fix (no behavior change).

Why:
- `tools/clang_format.sh --check` is part of verification gate; this file needed formatting for CI/local gate to pass.

### Files changed
- `docs/architecture_invariants.md` (new)
- `tests/labops/architecture_contract_check_smoke.cpp` (new)
- `CMakeLists.txt`
- `docs/README.md`
- `tests/labops/README.md`
- `tests/labops/soak_checkpoint_resilience_smoke.cpp` (format-only)
- `SUMMARY.md`

### Verification

1. Formatting
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build + focused checks
- `cmake --build build`
- `ctest --test-dir build -R "architecture_contract_check_smoke|run_stream_trace_smoke|run_threshold_failure_smoke|validate_actionable_smoke" --output-on-failure`
- Result: passed (4/4)

3. Full suite
- `ctest --test-dir build --output-on-failure`
- Result: passed (73/73)

### Notes
- No behavior changes to runtime contract logic were introduced.
- Contract checks now run with normal `ctest`, so they are CI-visible by default.
