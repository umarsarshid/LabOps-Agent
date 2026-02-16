# Triage Bundle Spec

## Purpose

This document defines the canonical triage bundle produced by `labops run`.
The bundle is the handoff package for camera test investigation. It is meant
to be used by:

- test engineers validating run quality and reproducibility
- on-call and triage engineers diagnosing failures
- future agent workflows that parse run artifacts automatically

Primary goals:

- reproducibility: rerun the exact scenario with exact recorded settings
- evidence quality: preserve timeline + metrics + run metadata together
- machine-readability: stable contracts for automation and diff tooling

## Scope

This spec covers:

- bundle directory layout
- required and optional files
- file-level contracts (`run.json`, `events.jsonl`, `metrics.*`, manifest)
- integrity behavior (`bundle_manifest.json`)
- operational validation checks

This spec does not yet cover:

- baseline diff artifacts
- agent-generated root-cause summaries
- proprietary hardware backend payload extensions

## Bundle Lifecycle

For each `labops run`, the CLI creates a unique run directory:

- root: `<out>/<run_id>/`
- optional archive: `<out>/<run_id>.zip` when `--zip` is provided

Current high-level write sequence:

1. `scenario.json` snapshot is copied into bundle.
2. optional identifier redaction is applied when `--redact` is requested.
3. `hostprobe.json` is written with host system snapshot.
4. raw NIC command outputs (`nic_*.txt`) are written (best-effort per platform).
5. optional netem impairment is applied when `--apply-netem` is requested.
6. for real-backend runs, `config_verify.json` is written after setting apply
   to record requested vs actual vs supported readback evidence.
7. for real-backend runs, `camera_config.json` is written as an engineer-facing
   config report (identity + curated node dump + missing/unsupported keys).
8. for real-backend runs, `config_report.md` is written as a markdown status
   table (`✅ applied`, `⚠ adjusted`, `❌ unsupported`) for quick triage.
9. `events.jsonl` receives lifecycle and frame events.
10. netem teardown is attempted on run exit when apply succeeded.
11. `run.json` is written after stream completion (or earlier on certain
   backend initialization failures so run metadata is still preserved).
12. `metrics.csv` and `metrics.json` are written.
13. `summary.md` is written as a one-page human triage report.
14. `report.html` is written as a static browser triage report (no JS).
15. `bundle_manifest.json` is generated from required artifacts.
16. optional `.zip` archive is created as a sibling of bundle directory.

## Directory Layout Contract

Required bundle structure:

```text
<out>/
  <run_id>/
    scenario.json
    hostprobe.json
    nic_*.txt
    run.json
    config_verify.json   # real-backend runs
    camera_config.json   # real-backend runs
    config_report.md     # real-backend runs
    events.jsonl
    metrics.csv
    metrics.json
    summary.md
    report.html
    bundle_manifest.json
```

Optional output:

```text
<out>/
  <run_id>.zip
```

`<run_id>` is generated as `run-<epoch_millis>`.

## Required Artifact Table

| File | Required | Producer | Why it exists |
| --- | --- | --- | --- |
| `scenario.json` | yes | scenario writer | Preserves exact scenario input used for run reproducibility. |
| `hostprobe.json` | yes | host probe writer | Captures host OS/CPU/RAM/uptime/load context and parsed NIC highlights (including MTU/link hints when available). |
| `nic_*.txt` | yes | host probe writer | Raw NIC command outputs (platform-specific command set). |
| `run.json` | yes | run writer | Captures run identity, immutable config, optional real-device identity/version metadata, and run timestamps. |
| `config_verify.json` | conditional (real backend) | config verify writer | Captures per-setting requested vs actual vs supported readback evidence after apply. |
| `camera_config.json` | conditional (real backend) | camera config writer | Captures resolved camera identity plus curated node rows and missing/unsupported key lists for engineer-readable config triage. |
| `config_report.md` | conditional (real backend) | config report writer | Provides a one-page markdown status table for applied/adjusted/unsupported settings without opening JSON artifacts. |
| `events.jsonl` | yes | event writer | Timeline-level evidence for stream behavior and failures. |
| `metrics.csv` | yes | metrics writer | Human-readable metrics for spreadsheets and quick plotting. |
| `metrics.json` | yes | metrics writer | Machine-readable metrics for automation and agent parsing. |
| `summary.md` | yes | summary writer | One-page human summary with pass/fail, key metrics, and top anomalies. |
| `report.html` | yes | html report writer | Static browser report with key metric and delta tables (plots-ready). |
| `bundle_manifest.json` | yes | manifest writer | Integrity and inventory contract for triage handoff. |
| `<run_id>.zip` | optional | zip writer | Portable support bundle for sharing outside raw workspace. |

## File Contracts

### `scenario.json`

- a byte copy of the source scenario file used in `labops run`
- stored at bundle root as `scenario.json`
- intended as immutable provenance for reruns and audits

### `run.json`

Canonical structure:

```json
{
  "run_id": "run-1771018464116",
  "config": {
    "scenario_id": "labops-scenario",
    "backend": "real_stub",
    "seed": 777,
    "duration_ms": 1200
  },
  "real_device": {
    "model": "SprintCam",
    "serial": "SN-2000",
    "transport": "usb",
    "user_id": "Secondary",
    "firmware_version": "4.0.0",
    "sdk_version": "21.1.8"
  },
  "timestamps": {
    "created_at_utc": "2026-02-13T21:34:24.116Z",
    "started_at_utc": "2026-02-13T21:34:24.116Z",
    "finished_at_utc": "2026-02-13T21:34:25.276Z"
  }
}
```

Field notes:

- `scenario_id`: currently derived from scenario filename stem
- `backend`: currently `sim` in default flow
- `real_device`: optional; present when run resolved a concrete real camera
  - `model`, `serial`, `transport` are required when `real_device` exists
  - `firmware_version` is included when the SDK/discovery source exposes it
  - `sdk_version` is always captured for real-device runs (`unknown` fallback)
- `timestamps.*`: UTC with millisecond precision (`YYYY-MM-DDTHH:MM:SS.mmmZ`)

### `camera_config.json`

Purpose:

- provide an engineer-readable config snapshot for real-backend runs
- capture "what was requested vs what actually applied" in one stable file

Current fields:

- `identity`:
  - resolved camera identity/version fields (`model`, `serial`, `transport`,
    `user_id`, `firmware_version`, `sdk_version`)
  - selector details when available (`selector`, `index`, optional `ip`, `mac`)
- `curated_nodes`:
  - fixed key rows for core camera settings (`frame_rate`, `pixel_format`,
    `exposure`, `gain`, `roi`, `trigger_mode`, `trigger_source`)
  - each row captures `requested`, `actual`, `supported`, `applied`,
    `adjusted`, `missing`, and `reason`
- `missing_keys`: curated keys with no readback row
- `missing_requested_keys`: requested keys that did not produce readback rows
- `unsupported_keys`: keys that were unsupported or unapplied
- `backend_dump`: raw backend config key/value snapshot for low-level context

### `config_report.md`

Purpose:

- provide a quick, human-readable config result summary for real-backend runs
- avoid opening JSON files during first-pass triage

Current sections:

- run identity block (`run_id`, `scenario_id`, `backend`, `apply_mode`)
- optional collection notes (when config collection had upstream errors)
- summary counters:
  - `✅ applied`
  - `⚠ adjusted`
  - `❌ unsupported`
- config status table:
  - columns: `Status`, `Key`, `Node`, `Requested`, `Actual`, `Notes`
  - each row is one config key outcome with explicit status icon/text

### `hostprobe.json`

Purpose:

- capture host context that often explains environment-specific failures
- keep a lightweight machine snapshot alongside run metrics/events

Current fields:

- `captured_at_utc`
- `os` object:
  - `name`, `version`
- `cpu` object:
  - `model`, `logical_cores`
- `ram_total_bytes`
- `uptime_seconds`
- `load_avg` object:
  - `one_min`, `five_min`, `fifteen_min` (null when unavailable)
- `nic_highlights` object:
  - `default_route_interface` (string or null)
  - `interfaces` array:
    - `name`
    - `mac_address` (string or null)
    - `ipv4_addresses` (array)
    - `ipv6_addresses` (array)
    - `mtu_hint` (integer or null)
    - `link_speed_hint` (string or null)
    - `has_default_route` (bool)

When `--redact` is enabled:

- obvious hostname tokens are replaced with `<redacted_host>`
- obvious username tokens are replaced with `<redacted_user>`

### `nic_*.txt`

Purpose:

- preserve raw network command evidence exactly as observed on the host
- provide low-level context for transport/NIC triage without rerunning probes

Platform command coverage:

- Windows:
  - `nic_ipconfig_all.txt` (`ipconfig /all`)
- Linux:
  - `nic_ip_a.txt` (`ip a`)
  - `nic_ip_r.txt` (`ip r`)
  - `nic_ethtool.txt` (`ethtool` per interface when available; otherwise
    explicit unavailable note)
- macOS:
  - `nic_ifconfig_a.txt` (`ifconfig -a`)
  - `nic_netstat_rn.txt` (`netstat -rn`)
  - `nic_route_get_default.txt` (`route -n get default`)

When `--redact` is enabled:

- obvious hostname and username tokens are replaced in raw command text before
  files are written.

### `events.jsonl`

Format:

- newline-delimited JSON (one event object per line)
- append-only for the life of the run
- each event has:
  - `ts_utc`: UTC timestamp string, millisecond precision
  - `type`: normalized event type string
  - `payload`: string key/value map

Current event types in run flow:

- `CONFIG_APPLIED`
- `CONFIG_UNSUPPORTED`
- `CONFIG_ADJUSTED`
- `STREAM_STARTED`
- `FRAME_RECEIVED`
- `FRAME_DROPPED`
- `STREAM_STOPPED`

Important timeline behavior:

- event lines are append-order, not guaranteed timestamp-sorted
- with fault injection (for example reorder), `ts_utc` may move backward
  between adjacent lines
- consumers should sort by timestamp only when doing timing analysis
  and preserve original line order for replay/debug traces

Payload conventions:

- reserved keys include `run_id`, `scenario_id` for run-level correlation
- backend-applied params are prefixed with `param.` inside `CONFIG_APPLIED`

### `metrics.csv`

Header is fixed:

`metric,window_end_ms,window_ms,frames,fps`

Row families:

- `avg_fps`
- `drops_total`
- `drop_rate_percent`
- `rolling_fps` (one row per rolling sample)
- `inter_frame_interval_min_us`
- `inter_frame_interval_avg_us`
- `inter_frame_interval_p95_us`
- `inter_frame_jitter_min_us`
- `inter_frame_jitter_avg_us`
- `inter_frame_jitter_p95_us`

Formatting:

- numeric values use fixed precision (6 decimals)

### `metrics.json`

Top-level contract:

- `avg_window_ms`
- `rolling_window_ms`
- `frames_total`
- `received_frames_total`
- `dropped_frames_total`
- `drop_rate_percent`
- `avg_fps`
- `inter_frame_interval_us` object:
  - `sample_count`, `min_us`, `avg_us`, `p95_us`
- `inter_frame_jitter_us` object:
  - `sample_count`, `min_us`, `avg_us`, `p95_us`
- `rolling_fps` array:
  - `window_end_ms`, `frames_in_window`, `fps`

### `summary.md`

Purpose:

- give engineers a quick one-page run verdict before deep-diving raw artifacts
- keep pass/fail, metric highlights, and anomaly hints in one readable file

Current sections:

- `Status` (`PASS` or `FAIL`)
- `Run Identity` (run_id, scenario_id, backend, seed, timestamps)
- `Key Metrics` table
- `Threshold Checks`
- `Top Anomalies`
  - currently surfaces named metric heuristics when detected:
    - `Resend spike`
    - `Jitter cliff`
    - `Periodic stall`
- optional `Netem Commands (Manual)` when scenario references a valid
  `netem_profile`

Execution note:
- when `--apply-netem` is used on Linux, commands are applied before stream and
  teardown is always attempted on exit after successful apply.

### `report.html`

Purpose:

- provide a browser-friendly triage view that does not require markdown tooling
- expose table-first metric and delta data ready for copy/paste into charting tools

Current sections:

- run status badge (`PASS` / `FAIL`)
- run identity table
- key metrics table
- `Diffs (Actual vs Expected)` table
- rolling FPS samples table (`window_end_epoch_ms`, `frames_in_window`, `fps`)
- threshold checks
- top anomalies

### `bundle_manifest.json`

Purpose:

- enumerate bundle artifacts that define the core evidence packet
- provide hash + size integrity checks for handoff and storage

Canonical structure:

```json
{
  "schema_version": "1.0",
  "hash_algorithm": "fnv1a_64",
  "files": [
    {
      "path": "events.jsonl",
      "size_bytes": 12345,
      "hash": "f95a8d9f1234abcd"
    }
  ]
}
```

Current manifest file inclusion list:

- `scenario.json`
- `hostprobe.json`
- `nic_*.txt`
- `run.json`
- `events.jsonl`
- `metrics.csv`
- `metrics.json`
- `summary.md`
- `report.html`

Inclusion behavior:

- paths are bundle-relative and sorted lexicographically
- only regular files are accepted
- files outside bundle directory are rejected

## Metric Definitions

### Shared Frame Terms

- `frames_total`: total frame samples returned by backend
- `received_frames_total`: count of non-dropped samples
- `dropped_frames_total`: count of dropped samples

### FPS Metrics

- `avg_fps`
  - formula: `received_frames_total / (avg_window_ms / 1000.0)`
  - dropped samples are excluded from numerator

- `rolling_fps`
  - fixed window: `rolling_window_ms` (currently 1000 ms)
  - per-sample formula:
    - `frames_in_window / (rolling_window_ms / 1000.0)`
  - inclusion window:
    - received frames with timestamps in
      `[window_end_ms - rolling_window_ms, window_end_ms]`

### Drop Metrics

- `drops_total`: equal to `dropped_frames_total`
- `drop_rate_percent`:
  - if `frames_total > 0`:
    - `(dropped_frames_total * 100.0) / frames_total`
  - else: `0.0`

### Inter-Frame Timing Metrics

Computed using received frame timestamps sorted ascending:

- intervals:
  - `interval_i = ts_i - ts_(i-1)` in microseconds
  - metrics: `min_us`, `avg_us`, `p95_us`
- jitter:
  - `jitter_i = abs(interval_i - mean(intervals))` in microseconds
  - metrics: `min_us`, `avg_us`, `p95_us`

P95 rule (nearest-rank):

- `rank = ceil(0.95 * N)`
- `p95 = sorted_values[rank - 1]`

## Edge Cases

- no frames (`frames_total == 0`)
  - `drop_rate_percent = 0.0`
  - `avg_fps = 0.0`
- fewer than 2 received frames
  - interval/jitter sample count is `0`
  - interval/jitter values remain zeroed
- dropped frames
  - excluded from FPS interval calculations
  - included in drop metrics

## Operational Verification

Use this checklist after changing bundle code:

1. Run one scenario without zip:
   - `labops run scenarios/sim_baseline.json --out out`
2. Confirm bundle layout exists at `out/<run_id>/`.
3. Confirm all required files exist.
   - includes at least one `nic_*.txt` raw network command artifact.
4. Validate JSONL is non-empty:
   - includes `CONFIG_APPLIED`, `STREAM_STARTED`, and `STREAM_STOPPED`
5. Confirm manifest references exactly current required artifacts.
6. Recompute at least one hash independently (optional spot-check).
7. Run one scenario with zip:
   - `labops run scenarios/sim_baseline.json --out out --zip`
8. Confirm `out/<run_id>.zip` exists and expands to same file set.

## Compatibility Guidance

- Treat file names and required fields as stable contract for tooling.
- Additive fields are preferred over breaking renames/removals.
- If breaking changes are required:
  - bump manifest `schema_version`
  - update this spec in the same change
  - update CI/integration assertions that consume bundle artifacts
