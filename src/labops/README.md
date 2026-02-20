# src/labops

`src/labops/` owns the CLI surface for the project.

## Why this folder exists

The CLI is the primary user interface for lab engineers and CI pipelines. Keeping CLI concerns here makes it easy to understand what commands exist, what arguments are valid, and what output/exit contracts scripts can trust.

## What belongs here

- Process entrypoint (`main.cpp`).
- Top-level command routing and argument handling.
- Command-level output and exit code contracts.
- CLI-owned support modules for run-specific workflows (for example soak
  checkpoint/frame-cache persistence under `soak/`).

## Run Pipeline Internals

The core run path in `cli/router.cpp` keeps the external command contract the
same, but is now split into explicit internal stages with shared
`RunExecutionContext` state:

- `PrepareRunContext`
- `InitializeArtifacts`
- `ConfigureBackend`
- `ExecuteStreaming`
- `FinalizeMetricsAndReports`
- `EmitFinalConsoleSummary`

Why this structure exists:
- keeps `labops run` / `baseline capture` behavior identical while reducing
  monolithic control flow.
- makes changes easier to review stage-by-stage (validation, artifacts,
  backend wiring, streaming, finalization, summary output).

## Current command contract

- `labops version`: prints tool version.
- `labops list-backends`: prints backend availability at a glance (for example
  `sim` enabled and whether `real` is enabled or disabled with reason).
- `labops list-devices --backend real|webcam`: device discovery contract.
  - `real`: lists real-backend devices when available; returns friendly
    `BACKEND_NOT_AVAILABLE` messaging when the real backend is disabled in
    current build/runtime configuration; when enabled, prints per-device
    identity fields (`model`, `serial`, `user_id`, `transport`, optional
    `ip`/`mac`, optional `firmware_version`/`sdk_version`).
  - `webcam`: lists webcam devices via fixture discovery, Linux native V4L2
    query (`/dev/video*` + `VIDIOC_QUERYCAP`), or OpenCV fallback probing and
    prints normalized identity fields (`id`, `friendly_name`, optional
    `bus_info`, optional `capture_index`) plus explicit supported-control
    rows (value type, ranges, enum values, and read-only hints).
- `labops validate <scenario.json>`: validates scenario schema and prints
  actionable field-level errors when invalid.
- `labops run <scenario.json> --out <dir> [--device <selector>] [--sdk-log] [--zip] [--redact] [--soak --checkpoint-interval-ms <ms> [--soak-stop-file <path>] [--resume <checkpoint.json>]] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`: emits a per-run bundle under
  `<dir>/<run_id>/` containing `scenario.json`, `run.json`,
  `config_verify.json` (real backend readback evidence),
  `camera_config.json` (real backend config report),
  `config_report.md` (real backend markdown triage report),
  optional `sdk_log.txt` when `--sdk-log` is enabled for real runs,
  `events.jsonl`,
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
  evidence; for real-device runs, `run.json` includes transport counter status
  (`resends`, `packet_errors`, `dropped_packets`) with either numeric values
  or explicit `not_available`;
  for real-backend runs, handles `Ctrl+C` (`SIGINT`) as a graceful interrupt
  request that stops frame collection at safe boundaries, writes a
  `STREAM_STOPPED` event with reason `signal_interrupt`, flushes all core run
  artifacts (`run.json`, `events.jsonl`, `metrics.*`, `summary.md`,
  `report.html`, `bundle_manifest.json`), and exits non-zero;
  for real-backend runs, treats mid-stream disconnects as retryable failures:
  emits `DEVICE_DISCONNECTED`, retries reconnect with a bounded budget, and if
  retries are exhausted still flushes a complete failure bundle with
  `STREAM_STOPPED` reason `device_disconnect`;
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
