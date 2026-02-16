# tests/labops

CLI-level integration smoke tests.

## Why this folder exists

Some behaviors are only meaningful when exercised through the `labops` command
surface (argument parsing + runtime orchestration + artifact/event output).

## Current contents

Most tests in this folder use shared helper headers from `tests/common/` for:
- assertion helpers (`AssertContains`, failure reporting)
- unique temp-root setup
- CLI `argv` dispatch boilerplate

This keeps each test focused on contract verification instead of setup code.

- `run_stream_trace_smoke.cpp`: drives `labops run` via CLI dispatch and
  verifies `events.jsonl` contains config-audit plus realistic stream lifecycle events and
  `metrics.csv`/`metrics.json` are emitted with expected metric fields,
  `summary.md` and `report.html` are readable, `hostprobe.json` includes key
  host + NIC highlight fields (including MTU/link hint keys), optional manual netem command
  suggestions appear in `summary.md` when `netem_profile` is set, raw NIC
  command outputs (`nic_*.txt`) are present, and
  `bundle_manifest.json` includes the core artifact entries and hash metadata.
- `validate_actionable_smoke.cpp`: drives `labops validate` and verifies
  invalid schema output includes actionable field-level error paths and the
  schema-invalid exit code contract.
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
  returns the thresholds-failed exit code from `labops run` while still
  producing core artifacts plus `summary.md`/`report.html` status views of
  `FAIL`.
- `run_backend_connect_failure_smoke.cpp`: verifies `backend: real_stub`
  triggers a deterministic backend connection failure and returns the
  backend-connect-failed exit code while still emitting early evidence
  (`scenario.json`, `hostprobe.json`, `run.json`).
- `netem_option_contract_smoke.cpp`: verifies netem execution flags are used
  safely (`--apply-netem` requires `--netem-iface <iface>` and vice versa).
- `logging_contract_smoke.cpp`: verifies structured run logs honor
  `--log-level` and include the same `run_id` written to `run.json` so logs
  correlate directly with bundle artifacts.
- `list_backends_smoke.cpp`: verifies `labops list-backends` prints deterministic
  availability output for `sim` plus `real` (enabled or disabled with reason).
- `list_devices_real_backend_smoke.cpp`: verifies
  `labops list-devices --backend real` returns device-list output when real
  backend is enabled (including normalized model/serial/user_id/transport and
  optional IP/MAC plus optional firmware/SDK version fields from descriptor
  fixture data) and friendly
  `BACKEND_NOT_AVAILABLE` messaging when disabled.
- `run_device_selector_resolution_smoke.cpp`: verifies
  `labops run ... --device <selector>` resolves the requested real device
  deterministically, logs selected identity/version fields, writes `run.json`
  with `real_device` metadata, writes `camera_config.json` with resolved
  identity + curated config fields, writes `config_report.md` for readable
  status review, and follows expected exit-code behavior for
  both real-enabled and real-disabled builds.
- `real_apply_mode_events_smoke.cpp`: verifies real-backend `apply_mode`
  behavior:
  `best_effort` records unsupported settings and continues (emitting
  `CONFIG_UNSUPPORTED`, `CONFIG_ADJUSTED`, and `CONFIG_APPLIED`) plus writes
  `config_verify.json`, `camera_config.json`, and `config_report.md` with
  requested/actual/supported evidence, while
  `strict` fails early on unsupported settings and does not emit
  `CONFIG_APPLIED` (but still writes both config evidence artifacts).
- `kb_draft_from_run_folder_smoke.cpp`: verifies `labops kb draft --run <dir>`
  generates `kb_draft.md` from `engineer_packet.md` with populated run-context,
  repro, hypothesis, and evidence-link sections.
- `soak_checkpoint_resume_smoke.cpp`: verifies long-run soak mode emits
  periodic checkpoints, supports safe pause at checkpoint boundaries, resumes
  from `soak_checkpoint.json`, and completes with full artifacts without
  losing previously captured evidence.

## Connection to the project

These tests validate the user-facing contract, not just individual modules,
which is critical for operator trust and CI confidence.
