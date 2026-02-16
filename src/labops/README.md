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
- `labops list-backends`: prints backend availability at a glance (for example
  `sim` enabled and whether `real` is enabled or disabled with reason).
- `labops list-devices --backend real`: lists real-backend devices when
  available; returns friendly `BACKEND_NOT_AVAILABLE` messaging when the real
  backend is disabled in current build/runtime configuration; when enabled,
  prints per-device identity fields (`model`, `serial`, `user_id`, `transport`,
  optional `ip`/`mac`, optional `firmware_version`/`sdk_version`).
- `labops validate <scenario.json>`: validates scenario schema and prints
  actionable field-level errors when invalid.
- `labops run <scenario.json> --out <dir> [--device <selector>] [--zip] [--redact] [--soak --checkpoint-interval-ms <ms> [--soak-stop-file <path>] [--resume <checkpoint.json>]] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`: emits a per-run bundle under
  `<dir>/<run_id>/` containing `scenario.json`, `run.json`,
  `config_verify.json` (real backend readback evidence),
  `camera_config.json` (real backend config report),
  `config_report.md` (real backend markdown triage report), `events.jsonl`,
  `metrics.csv`, `metrics.json`, `summary.md`, `report.html`,
  `hostprobe.json`, and
  platform NIC raw command outputs (`nic_*.txt`),
  `bundle_manifest.json`; optionally emits `<dir>/<run_id>.zip` when `--zip`
  is set; optionally redacts obvious host/user identifiers in hostprobe outputs
  when `--redact` is set; optionally applies Linux netem impairment when
  explicitly requested; optionally runs in long-run soak mode with periodic
  checkpoints (`soak_checkpoint.json`, `checkpoints/checkpoint_*.json`) and
  resumable frame cache (`soak_frames.jsonl`) so evidence survives safe
  stop/resume; runs sim backend lifecycle;
  when backend is `real_stub`, optional `--device` resolves one connected
  device by `serial:<value>` or `user_id:<value>` and optional `index:<n>`
  before connect so run selection is deterministic; writes `run.json` early on
  backend-connect failure so selected device metadata is still preserved in
  evidence;
  evaluates configured scenario thresholds against computed metrics; returns
  non-zero when thresholds fail; and reports output paths.
- `labops baseline capture <scenario.json> [--redact] [--device <selector>] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`: captures a scenario baseline into
  `baselines/<scenario_id>/` using the same run pipeline and metrics writers as
  `labops run`, so release/regression comparisons use identical evidence math
  and optional identifier redaction behavior; supports the same selector
  format used by `run`.
- `labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]`:
  computes metric deltas and emits `diff.json` + `diff.md` (default output
  directory is the `--run` target when `--out` is omitted).
- `labops kb draft --run <run_folder> [--out <kb_draft.md>]`: reads
  `engineer_packet.md` from a run folder and writes a KB-ready markdown draft
  (`kb_draft.md`) pre-filled with context, repro steps, ruled-out checks, and
  evidence links.

## What should not live here

- Camera/stream runtime logic.
- Scenario schema internals.
- Metric computation internals.
- Agent planning heuristics.

Those belong in their dedicated modules and are called from here.

## Connection to the project

A reliable CLI contract is critical because everything else (humans, CI, future orchestration services) invokes `labops` through this interface.
