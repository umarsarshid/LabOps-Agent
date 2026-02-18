# Commit Summary

## Commit
`feat(real): handle clean shutdown on Ctrl+C and flush run artifacts`

## What I Implemented

This commit adds graceful Ctrl+C handling for real-backend `labops run` execution so interrupted runs still produce a valid, engineer-usable evidence bundle.

### In plain terms
- Pressing Ctrl+C during a real run no longer risks losing the run packet.
- LabOps now stops at safe boundaries, writes `STREAM_STOPPED` with reason `signal_interrupt`, flushes bundle files, and exits non-zero.

## Why This Was Needed

Before this change, interrupt behavior was only handled for soak pause flow. A normal real run interrupted by Ctrl+C could terminate before all writers finished, leaving incomplete evidence.

For triage workflows, partial or missing artifacts are high-cost because engineers cannot trust reproducibility or compare results cleanly.

## Detailed Changes

## 1) Unified SIGINT handling at run scope
File:
- `src/labops/cli/router.cpp`

Changes:
- Replaced soak-specific signal flag with run-level interrupt flag:
  - `g_run_interrupt_requested`
- Updated SIGINT handler to set this run-level flag.
- Updated `ResolveSoakStopReason()` to read the shared interrupt flag.
- Simplified `ScopedInterruptSignalHandler` to always arm/disarm for run execution.

Why:
- one consistent interrupt signal source avoids split behavior between soak and normal runs.
- guarantees SIGINT is captured during run lifecycle instead of default abrupt process termination.

## 2) Added real-backend safe-boundary interrupt loop for non-soak runs
File:
- `src/labops/cli/router.cpp`

Changes:
- For non-soak real backend (`backend == real_stub`), frame pulling now happens in 250ms chunks.
- At each chunk boundary, run checks `g_run_interrupt_requested`.
- On interrupt request:
  - marks run as interrupted,
  - records completed vs requested duration,
  - continues to flush downstream artifacts.

Important compatibility guard:
- Sim non-soak runs keep one-shot pull behavior (no chunking) to preserve existing deterministic frame-count behavior and avoid per-chunk rounding drift.

Why:
- chunk boundaries create safe cancellation points for real runs.
- preserving sim one-shot behavior prevents regressions in existing tests and baseline expectations.

## 3) Added explicit interrupted stop-reason + payload details
File:
- `src/labops/cli/router.cpp`

Changes:
- `STREAM_STOPPED` reason now uses `signal_interrupt` when interrupted.
- Added interruption payload fields:
  - `requested_duration_ms`
  - `completed_duration_ms`

Why:
- stream stop reason must clearly distinguish a user interrupt from normal completion.
- duration deltas are useful for triage context and run reproducibility notes.

## 4) Kept metrics/summaries valid for interrupted runs
File:
- `src/labops/cli/router.cpp`

Changes:
- For interrupted real non-soak runs, metrics compute over completed duration (min 1ms fallback).
- Threshold evaluation is intentionally skipped in favor of deterministic interrupted-run failure messaging:
  - inserts threshold failure note: `run interrupted by signal before requested duration completed`
- CLI prints:
  - `run_status: interrupted`
  - completed/requested duration values
- Returns non-zero (`kFailure`) for interrupted runs.

Why:
- interrupted runs should still emit coherent artifacts while signaling incomplete execution to automation.
- explicit interruption reason in summary is more actionable than ambiguous threshold math on unfinished runs.

## 5) Added integration smoke test for Ctrl+C flush contract
Files:
- `tests/labops/run_interrupt_flush_smoke.cpp` (new)
- `CMakeLists.txt`
- `tests/labops/README.md`

Test behavior:
- Creates a real-backend scenario with long duration.
- Starts `labops run` and sends SIGINT from a helper thread.
- Verifies:
  - non-zero exit code (`kFailure`),
  - required bundle files exist (`run.json`, `events.jsonl`, `metrics.csv`, `metrics.json`, `summary.md`, `report.html`, `bundle_manifest.json`, etc.),
  - `events.jsonl` includes `STREAM_STOPPED` with reason `signal_interrupt`,
  - summary contains interruption note.
- If real backend is disabled at build, test exits cleanly (no false failure in disabled builds).

Why:
- protects the new interrupt behavior with a CLI-level regression test.
- validates done-condition directly: interrupted runs still yield valid artifacts.

## 6) Updated module docs for operator clarity
File:
- `src/labops/README.md`

Changes:
- Documented real-run Ctrl+C graceful interrupt behavior and resulting artifact guarantees.

Why:
- user-facing command contract should describe interruption semantics explicitly.

## Verification Performed

## Formatting
- `bash tools/clang_format.sh --check` (initially failed)
- `bash tools/clang_format.sh --fix`
- `bash tools/clang_format.sh --check`
- Result: pass

## Build
- `cmake --build build`
- Result: pass

## Focused tests
- `ctest --test-dir build -R "run_interrupt_flush_smoke|run_stream_trace_smoke|sim_determinism_golden_smoke|starter_scenarios_e2e_smoke|run_threshold_failure_smoke|soak_checkpoint_resume_smoke" --output-on-failure`
- Result: pass (`6/6`)

## Full suite
- `ctest --test-dir build --output-on-failure`
- Result: pass (`59/59`)

## Risk Assessment

Low to moderate:
- touches central run pipeline signal/termination behavior.
- mitigated by preserving sim behavior, adding dedicated interrupt smoke test, and full-suite pass.

## Outcome

Real backend runs now handle Ctrl+C as a graceful interruption path that still writes a complete, valid artifact bundle with explicit interruption evidence, improving reliability for real-world triage handoff.
