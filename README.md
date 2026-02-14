# LabOps Agent

C++-first autopilot lab assistant for repeatable camera testing and faster triage.

## What This Project Is

LabOps Agent runs camera scenarios the same way every time, captures evidence
automatically, and prepares the foundation for AI-assisted root-cause
isolation.

Simple flow:
- You run a scenario.
- LabOps executes the backend with deterministic controls.
- It records a per-run bundle with `scenario.json`, `run.json`,
  `events.jsonl`, `metrics.csv`, `metrics.json`, `summary.md`, and
  `hostprobe.json` plus raw NIC command outputs (`nic_*.txt`).
- Tests verify contracts and determinism so behavior stays reproducible.

## Current Status

The repo currently has a working CLI, deterministic sim backend, scenario pack,
artifact/event/metrics output pipeline, and CI-validated integration tests.
Bundle packaging is implemented (manifest + optional zip), and triage bundle
contracts are documented as an internal tooling spec in
`docs/triage_bundle_spec.md`. Runs now also emit a one-page `summary.md`
artifact for quick human triage. Agent diagnosis/planning is still upcoming.

## Milestone Progress

- Milestone 0: done (repo/build/style/CI foundation)
- Milestone 1: done (CLI skeleton + strict output contracts)
- Milestone 2: done (sim backend + deterministic/fault-injected stream runs)
- Milestone 3: done (FPS/drop/jitter metrics + metric artifacts)
- Milestone 4: done (scenario schema, validation, scenario application in run)
- Milestone 5: done (bundle layout, manifest, optional support zip, bundle docs)
- Milestone 6: done (baseline capture + compare diff outputs + threshold pass/fail)
- Milestone 7: in progress (run summary + host probe evidence)

## Implemented So Far

- Project/build foundation with CMake.
- Cross-platform CI (`ubuntu`, `macos`, `windows`) with `ctest`.
- CLI commands:
  - `labops version`
  - `labops validate <scenario.json>`
  - `labops run <scenario.json> [--out <dir>] [--zip]`
  - `labops baseline capture <scenario.json>`
  - `labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]`
- Scenario loader + schema validation in `labops validate` with actionable
  field-level errors.
- Starter scenario set in `scenarios/`:
  - `sim_baseline.json`
  - `dropped_frames.json`
  - `trigger_roi.json`
- Run contract schema (`RunConfig`, `RunInfo`) with JSON serialization.
- Artifact writers for per-run bundle files:
  - `<out>/<run_id>/scenario.json`
  - `<out>/<run_id>/hostprobe.json`
  - `<out>/<run_id>/nic_*.txt`
  - `<out>/<run_id>/run.json`
  - `<out>/<run_id>/events.jsonl`
  - `<out>/<run_id>/metrics.csv`
  - `<out>/<run_id>/metrics.json`
  - `<out>/<run_id>/summary.md`
  - `<out>/<run_id>/bundle_manifest.json`
  - optional `<out>/<run_id>.zip` support bundle when `--zip` is requested
- Baseline capture command:
  - `labops baseline capture <scenario.json>`
  - writes baseline artifacts directly under `baselines/<scenario_id>/`
  - baseline directory includes `metrics.csv`, `metrics.json`, `summary.md`,
    `hostprobe.json`, and raw NIC command outputs (`nic_*.txt`)
- Compare command:
  - `labops compare --baseline ... --run ...`
  - compares summary metrics and writes `diff.json` + `diff.md`
  - writes compare outputs to run target by default (or `--out <dir>`)
- Threshold enforcement in run flow:
  - evaluates scenario thresholds after metrics computation
  - returns non-zero on threshold violations
- Stream lifecycle event emission in `labops run`:
  - `CONFIG_APPLIED`
  - `STREAM_STARTED`
  - `FRAME_RECEIVED`
  - `FRAME_DROPPED`
  - `STREAM_STOPPED`
- FPS metrics pipeline:
  - computes `avg_fps` over run duration
  - computes `rolling_fps` over a fixed window
  - computes drop stats (`total dropped`, `drop rate percent`)
  - computes inter-frame interval stats (`min/avg/p95-ish`)
  - computes inter-frame jitter stats (`min/avg/p95-ish`)
  - writes `<out>/<run_id>/metrics.csv` and `<out>/<run_id>/metrics.json`
- One-page run summary pipeline:
  - writes `<out>/<run_id>/summary.md`
  - includes run status (`PASS`/`FAIL`), key metrics, threshold findings, and
    top anomalies
- Host probe pipeline:
  - writes `<out>/<run_id>/hostprobe.json`
  - includes OS/CPU/RAM/uptime/load snapshot and parsed NIC highlights for
    hardware/software triage
  - writes platform NIC raw command outputs as `nic_*.txt`
- Backend contract (`ICameraBackend`) plus deterministic sim backend.
- Sim features:
  - Configurable FPS, jitter, seed, frame size.
  - Fault knobs (`drop_every_n`, `drop_percent`, `burst_drop`, `reorder`).
  - Deterministic output for the same seed/scenario.
- Test coverage:
  - Schema JSON smoke tests.
  - Artifact writer smoke tests.
  - Event JSONL smoke tests.
  - Backend interface, frame generation, and fault injection smoke tests.
  - CLI run trace smoke test.
  - Starter scenarios end-to-end smoke test.
  - Metrics writer smoke test (`metrics.csv` + `metrics.json`).
  - Jitter injection smoke test.
  - Drop injection smoke test.
  - Determinism golden smoke test (same seed => same first K normalized events).
- Baseline scenario integration smoke test validating expected metric ranges.
- Baseline capture smoke test validating `baselines/<scenario_id>/` output contract.
- Compare diff smoke test validating `diff.json` + `diff.md` delta generation.
- Threshold failure smoke test validating non-zero `labops run` exit on violations.
- Run summary smoke coverage validating `summary.md` presence/readability.
- Catch2 core unit tests for schema/event JSON serialization (when available).
- Internal triage bundle spec in `docs/triage_bundle_spec.md` covering:
  - bundle lifecycle and directory contract
  - required/optional artifacts with file-level contracts
  - manifest integrity behavior
  - operational verification checklist and compatibility guidance

## Not Implemented Yet

- Scenario schema expansion (current validator covers core fields and
  constraints; deeper domain-specific rules can be added).
- Full metrics suite completion (disconnect-specific metrics beyond current FPS+drop+jitter timing).
- SDK-backed camera implementation (only interface boundary/stub exists).
- Agent experiment planner/runner and final engineer packet generation.

## Quick Start

### 1) Configure and build

```bash
cmake -S . -B build
cmake --build build
```

### 2) Check CLI

```bash
./build/labops version
```

### 3) Validate a starter scenario

```bash
./build/labops validate scenarios/sim_baseline.json
```

### 4) Run baseline scenario

```bash
./build/labops run scenarios/sim_baseline.json --out out/
```

### 5) Run fault-repro scenario (optional)

```bash
./build/labops run scenarios/dropped_frames.json --out out-drops/
```

### 6) Create support zip on demand (optional)

```bash
./build/labops run scenarios/sim_baseline.json --out out/ --zip
```

### 7) Capture baseline metrics for a scenario (optional)

```bash
./build/labops baseline capture scenarios/sim_baseline.json
```

### 8) Compare baseline vs run (optional)

```bash
./build/labops compare --baseline baselines/sim_baseline --run out/<run_id>
```

Expected bundle layout per run:
- `<out-dir>/<run_id>/scenario.json`
- `<out-dir>/<run_id>/hostprobe.json`
- `<out-dir>/<run_id>/nic_*.txt`
- `<out-dir>/<run_id>/run.json`
- `<out-dir>/<run_id>/events.jsonl`
- `<out-dir>/<run_id>/metrics.csv`
- `<out-dir>/<run_id>/metrics.json`
- `<out-dir>/<run_id>/summary.md`
- `<out-dir>/<run_id>/bundle_manifest.json`
- optional `<out-dir>/<run_id>.zip` when `--zip` is used

If you want to author new scenarios, follow `docs/scenario_schema.md`.

## Output Contracts

### Bundle Directory

- `labops run` now writes a dedicated bundle directory per run:
  - `<out>/<run_id>/`
- This keeps repeated runs under the same `--out` root isolated and easy to share.

### Support Zip

- `labops run --zip` writes an additional support archive:
  - `<out>/<run_id>.zip`
- Zip generation is opt-in so default runs avoid extra packaging overhead.

### Baseline Capture

- `labops baseline capture <scenario.json>` writes artifacts to:
  - `baselines/<scenario_id>/`
- This folder is the stable baseline target for future regression comparison.
- Baseline capture currently emits the same core evidence set as run mode,
  including `metrics.csv`, `metrics.json`, `summary.md`, `hostprobe.json`,
  and `nic_*.txt`.

### Compare Diff Output

- `labops compare --baseline ... --run ...` reads both `metrics.csv` files and
  computes per-metric deltas.
- Output artifacts:
  - `diff.json` (machine-readable deltas)
  - `diff.md` (human-readable delta table)
- Compare outputs default to the run target unless `--out <dir>` is provided.

### Threshold Pass/Fail

- `labops run` checks scenario threshold fields against computed metrics.
- Violations currently include:
  - `avg_fps < min_avg_fps`
  - `drop_rate_percent > max_drop_rate_percent`
  - `inter_frame_interval_p95_us > max_inter_frame_interval_p95_us`
  - `inter_frame_jitter_p95_us > max_inter_frame_jitter_p95_us`
- On violation:
  - process exits non-zero
  - bundle artifacts are still written for investigation

### scenario.json

- Byte-for-byte snapshot copy of the source scenario used for the run.
- Stored at `<out>/<run_id>/scenario.json`.

### hostprobe.json

- Host context snapshot written by `labops run`.
- Includes OS, CPU, RAM total bytes, uptime, load snapshot, and parsed NIC
  highlights.
- Stored at `<out>/<run_id>/hostprobe.json`.

### nic_*.txt

- Raw NIC command outputs captured best-effort per platform.
- Examples:
  - Windows: `nic_ipconfig_all.txt`
  - Linux: `nic_ip_a.txt`, `nic_ip_r.txt`, `nic_ethtool.txt` (if available)
  - macOS: `nic_ifconfig_a.txt`, `nic_netstat_rn.txt`, `nic_route_get_default.txt`
- Stored at `<out>/<run_id>/nic_*.txt`.

### bundle_manifest.json

- Lists bundle artifacts with relative file paths, `size_bytes`, and hash values.
- Current hash algorithm: `fnv1a_64`.
- Stored at `<out>/<run_id>/bundle_manifest.json`.

### summary.md

- One-page human-readable run report written by `labops run`.
- Includes:
  - run status (`PASS`/`FAIL`)
  - key identity fields (`run_id`, `scenario_id`, backend, seed, timestamps)
  - key metrics table (FPS/drop/timing highlights)
  - threshold findings and top anomalies
- Stored at `<out>/<run_id>/summary.md`.

### run.json

Current fields:
- `run_id`
- `config.scenario_id`
- `config.backend`
- `config.seed`
- `config.duration_ms`
- `timestamps.created_at_utc`
- `timestamps.started_at_utc`
- `timestamps.finished_at_utc`

### events.jsonl

- Append-only timeline log.
- One JSON object per line.
- Current `labops run` emits stream/frame lifecycle events:
  - `CONFIG_APPLIED`
  - `STREAM_STARTED`
  - `FRAME_RECEIVED`
  - `FRAME_DROPPED`
  - `STREAM_STOPPED`

### metrics.csv

- CSV metrics artifact written by `labops run`.
- Includes:
  - one `avg_fps` summary row
  - one `rolling_fps` row per rolling sample window
  - drop summary rows (`drops_total`, `drop_rate_percent`)
  - inter-frame interval `min/avg/p95` rows (microseconds)
  - inter-frame jitter `min/avg/p95` rows (microseconds)

### metrics.json

- JSON metrics artifact written by `labops run`.
- Includes machine-friendly structured fields for:
  - FPS summaries
  - drop summaries
  - inter-frame timing/jitter summaries
  - rolling FPS samples

For exact metric definitions (formulas, units, CSV/JSON contracts, and edge
cases), see `docs/triage_bundle_spec.md`.
For a full internal bundle contract (lifecycle, required files, manifest rules,
verification checklist), use `docs/triage_bundle_spec.md` as the authoritative
reference.

## Testing

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Run key integration smoke tests directly

```bash
ctest --test-dir build -R starter_scenarios_e2e_smoke --output-on-failure
ctest --test-dir build -R sim_baseline_metrics_integration_smoke --output-on-failure
```

### Run determinism test directly

```bash
./build/sim_determinism_golden_smoke
```

### Catch2 unit tests (optional)

Catch2 tests are under `tests/core/`. If Catch2 is not installed locally, enable
fetching:

```bash
cmake -S . -B build -DLABOPS_FETCH_CATCH2=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## CI

GitHub Actions:
- Builds on `ubuntu-latest`, `macos-latest`, and `windows-latest`.
- Runs `ctest` on PRs and pushes.

## Repository Guide

- `src/README.md`
- `docs/README.md`
- `docs/triage_bundle_spec.md`
- `docs/release_verification.md`
- `tests/README.md`
- `scenarios/README.md`

Most `src/` and `tests/` subfolders also include focused `README.md` files.

## Near-Term Roadmap

- Start agent experiment loop (change one variable at a time).
- Generate engineer packet output (repro steps, evidence, likely cause, next steps).
- Add hardware SDK backend implementation behind `ICameraBackend`.
