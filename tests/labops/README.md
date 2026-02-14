# tests/labops

CLI-level integration smoke tests.

## Why this folder exists

Some behaviors are only meaningful when exercised through the `labops` command
surface (argument parsing + runtime orchestration + artifact/event output).

## Current contents

- `run_stream_trace_smoke.cpp`: drives `labops run` via CLI dispatch and
  verifies `events.jsonl` contains config-audit plus realistic stream lifecycle events and
  `metrics.csv`/`metrics.json` are emitted with expected metric fields,
  `summary.md` is readable, `hostprobe.json` includes key host + NIC highlight
  fields (including MTU/link hint keys), raw NIC command outputs (`nic_*.txt`)
  are present, and
  `bundle_manifest.json` includes the core artifact entries and hash metadata.
- `validate_actionable_smoke.cpp`: drives `labops validate` and verifies
  invalid schema output includes actionable field-level error paths.
- `sim_determinism_golden_smoke.cpp`: runs the same seeded scenario twice and
  verifies the first `K` normalized events are identical.
- `starter_scenarios_e2e_smoke.cpp`: runs the starter scenario set
  (`sim_baseline.json`, `dropped_frames.json`, `trigger_roi.json`) end-to-end
  and verifies run artifacts + core lifecycle events are emitted for each.
- `bundle_layout_consistency_smoke.cpp`: runs baseline scenario twice against
  the same `--out` root and verifies standardized bundle layout
  (`<out>/<run_id>/...`) with required artifact files per run, including
  `bundle_manifest.json`.
- `bundle_zip_on_demand_smoke.cpp`: verifies `--zip` creates
  `<out>/<run_id>.zip` and default runs do not emit zip files.
- `baseline_capture_smoke.cpp`: verifies `labops baseline capture
  <scenario.json>` writes baseline artifacts directly under
  `baselines/<scenario_id>/` and includes metrics outputs for regression
  comparison.
- `compare_diff_smoke.cpp`: verifies `labops compare --baseline ... --run ...`
  generates `diff.json` + `diff.md` and reports non-zero deltas for a
  fault-injected run against baseline.
- `run_threshold_failure_smoke.cpp`: verifies a threshold-violating scenario
  returns non-zero from `labops run` while still producing core artifacts and
  a `summary.md` status of `FAIL`.

## Connection to the project

These tests validate the user-facing contract, not just individual modules,
which is critical for operator trust and CI confidence.
