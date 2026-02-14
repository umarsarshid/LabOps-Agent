# Release Verification Checklist

Use this checklist before cutting a release so we confirm core run quality and
regression signals with a repeatable baseline workflow.

## Goal

Confirm that:
- baseline capture works and produces expected artifacts
- a fresh run produces expected bundle artifacts
- compare flow works and produces `diff.json` + `diff.md`
- no unexpected metric regressions are introduced

## Preconditions

- project builds successfully:
  - `cmake -S . -B build`
  - `cmake --build build`
- key smoke tests pass:
  - `ctest --test-dir build --output-on-failure`
- you are in repo root

## Checklist

- [ ] 1. Capture baseline for release scenario.
  - command:
    - `./build/labops baseline capture scenarios/sim_baseline.json`
  - expected:
    - folder exists: `baselines/sim_baseline/`
    - includes: `scenario.json`, `run.json`, `events.jsonl`, `metrics.csv`, `metrics.json`, `summary.md`, `bundle_manifest.json`

- [ ] 2. Execute candidate run.
  - command:
    - `./build/labops run scenarios/sim_baseline.json --out out-release/`
  - expected:
    - creates one bundle directory: `out-release/run-<epoch_ms>/`
    - includes same core artifacts as baseline flow

- [ ] 3. Compare candidate run against baseline.
  - command template:
    - `./build/labops compare --baseline baselines/sim_baseline --run out-release/<run_id>`
  - expected:
    - writes `diff.json` and `diff.md` to the run bundle directory (unless `--out` is set)
    - compare command exits zero

- [ ] 4. Review metric deltas in `diff.md`.
  - check for unexpected changes in:
    - `avg_fps`
    - `drop_rate_percent`
    - `inter_frame_interval_*`
    - `inter_frame_jitter_*`
  - expected:
    - changes are either zero or explained/accepted for this release

- [ ] 5. Confirm threshold behavior still gates failures.
  - command:
    - `./build/run_threshold_failure_smoke`
  - expected:
    - smoke test passes
    - verifies `labops run` still returns non-zero on threshold violations

## Pass/Fail Rule

Release verification is considered **PASS** only when:
- baseline capture succeeded
- compare flow succeeded
- `diff.md` review is acceptable
- threshold gating smoke test passed

If any item fails, mark release verification as **FAIL** and do not cut the
release until resolved.
