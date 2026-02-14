# src/labops

`src/labops/` owns the CLI surface for the project.

## Why this folder exists

The CLI is the primary user interface for lab engineers and CI pipelines. Keeping CLI concerns here makes it easy to understand what commands exist, what arguments are valid, and what output/exit contracts scripts can trust.

## What belongs here

- Process entrypoint (`main.cpp`).
- Top-level command routing and argument handling.
- Command-level output and exit code contracts.

## Current command contract

- `labops version`: prints tool version.
- `labops validate <scenario.json>`: validates scenario schema and prints
  actionable field-level errors when invalid.
- `labops run <scenario.json> --out <dir> [--zip] [--redact] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`: emits a per-run bundle under
  `<dir>/<run_id>/` containing `scenario.json`, `run.json`, `events.jsonl`,
  `metrics.csv`, `metrics.json`, `summary.md`, `hostprobe.json`, and
  platform NIC raw command outputs (`nic_*.txt`),
  `bundle_manifest.json`; optionally emits `<dir>/<run_id>.zip` when `--zip`
  is set; optionally redacts obvious host/user identifiers in hostprobe outputs
  when `--redact` is set; optionally applies Linux netem impairment when
  explicitly requested; runs sim backend lifecycle;
  evaluates configured scenario thresholds against computed metrics; returns
  non-zero when thresholds fail; and reports output paths.
- `labops baseline capture <scenario.json> [--redact] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`: captures a scenario baseline into
  `baselines/<scenario_id>/` using the same run pipeline and metrics writers as
  `labops run`, so release/regression comparisons use identical evidence math
  and optional identifier redaction behavior.
- `labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]`:
  computes metric deltas and emits `diff.json` + `diff.md` (default output
  directory is the `--run` target when `--out` is omitted).

## What should not live here

- Camera/stream runtime logic.
- Scenario schema internals.
- Metric computation internals.
- Agent planning heuristics.

Those belong in their dedicated modules and are called from here.

## Connection to the project

A reliable CLI contract is critical because everything else (humans, CI, future orchestration services) invokes `labops` through this interface.
