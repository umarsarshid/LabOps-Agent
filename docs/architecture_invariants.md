# Architecture Invariants

This document captures contract-level behavior that other tooling and teams
depend on. These are invariants, not implementation details.

## Why This Exists

LabOps has stable behavior used by engineers, CI jobs, and downstream scripts.
If these contracts drift silently, triage workflows break even when code still
"works" locally.

This doc makes those expectations explicit and points to lightweight checks that
verify them continuously.

## Invariant Categories

### 1) Bundle Artifact Names and Layout

For `labops run ... --out <root>`, a successful run writes one per-run bundle:

- `<root>/<run_id>/scenario.json`
- `<root>/<run_id>/run.json`
- `<root>/<run_id>/events.jsonl`
- `<root>/<run_id>/metrics.csv`
- `<root>/<run_id>/metrics.json`
- `<root>/<run_id>/summary.md`
- `<root>/<run_id>/report.html`
- `<root>/<run_id>/bundle_manifest.json`

Additional context artifacts may also exist (for example `hostprobe.json`,
`nic_*.txt`, config reports).

The invariant is that core artifact names above remain stable.

### 2) Stable Exit Code Contract

CLI exit codes are stable and documented in `src/core/errors/exit_codes.hpp`:

- success: `0`
- generic failure: `1`
- usage error: `2`
- schema invalid: `10`
- backend connect failed: `20`
- thresholds failed: `30`

Automation should branch on these codes instead of stderr text parsing.

### 3) Event Stream Contract

`events.jsonl` lines are JSON objects with:

- `ts_utc` (timestamp string)
- `type` (stable event type string)
- `payload` (string map)

For normal run flow, stream lifecycle invariants include at least:

- `STREAM_STARTED`
- `STREAM_STOPPED`

`STREAM_STARTED` payload includes run identity/config fields (for example
`run_id`, `scenario_id`, `backend`, `duration_ms`).

`STREAM_STOPPED` payload includes frame counters (`frames_total`,
`frames_received`, `frames_dropped`).

### 4) Threshold Semantics

Configured threshold violations must produce:

- non-zero thresholds exit code (`30`)
- generated artifacts still present for debugging
- run summary marked as `FAIL`

Threshold pass/fail is part of the external run contract.

### 5) Run and Metrics Structural Fields

`run.json` and `metrics.json` keep stable top-level fields used by tooling:

- `run.json`: `run_id`, `config`, `timestamps`
- `metrics.json`: `avg_fps`, `frames_total`, dropped/timeout/incomplete counts,
  interval/jitter timing objects

Field additions are allowed, but existing contract fields should not be renamed
or removed without deliberate migration.

## Lightweight Contract Checks

Primary smoke test target:

- `architecture_contract_check_smoke`
  - validates core bundle file names
  - validates representative event payload keys
  - validates stable pass/fail exit-code behavior
  - validates threshold-failure summary behavior
  - validates schema-invalid exit behavior

This test is wired into `ctest` and therefore CI.

## Change Policy

When a contract-level invariant must change:

1. Update this document.
2. Update/extend the contract smoke checks.
3. Update downstream docs (`README.md`, operator docs) as needed.
4. Call out the change explicitly in commit summary/release notes.
