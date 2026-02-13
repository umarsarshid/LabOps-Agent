# tests/labops

CLI-level integration smoke tests.

## Why this folder exists

Some behaviors are only meaningful when exercised through the `labops` command
surface (argument parsing + runtime orchestration + artifact/event output).

## Current contents

- `run_stream_trace_smoke.cpp`: drives `labops run` via CLI dispatch and
  verifies `events.jsonl` contains config-audit plus realistic stream lifecycle events and
  `metrics.csv` and `metrics.json` are both emitted with expected metric fields.
- `validate_actionable_smoke.cpp`: drives `labops validate` and verifies
  invalid schema output includes actionable field-level error paths.
- `sim_determinism_golden_smoke.cpp`: runs the same seeded scenario twice and
  verifies the first `K` normalized events are identical.
- `starter_scenarios_e2e_smoke.cpp`: runs the starter scenario set
  (`sim_baseline.json`, `dropped_frames.json`, `trigger_roi.json`) end-to-end
  and verifies run artifacts + core lifecycle events are emitted for each.

## Connection to the project

These tests validate the user-facing contract, not just individual modules,
which is critical for operator trust and CI confidence.
