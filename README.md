# LabOps Agent

C++-first autopilot lab assistant for repeatable camera testing and faster triage.

## What This Project Is

LabOps Agent runs camera scenarios the same way every time, captures evidence
automatically, and prepares the foundation for AI-assisted root-cause
isolation.

Simple flow:
- You run a scenario.
- LabOps executes the backend with deterministic controls.
- It records `run.json`, `events.jsonl`, `metrics.csv`, and `metrics.json`.
- Tests verify contracts and determinism so behavior stays reproducible.

## Current Status

The repo currently has a working CLI, deterministic sim backend, scenario pack,
artifact/event/metrics output pipeline, and CI-validated integration tests.
Agent diagnosis/planning is still upcoming.

## Implemented So Far

- Project/build foundation with CMake.
- Cross-platform CI (`ubuntu`, `macos`, `windows`) with `ctest`.
- CLI commands:
  - `labops version`
  - `labops validate <scenario.json>`
  - `labops run <scenario.json> [--out <dir>]`
- Scenario loader + schema validation in `labops validate` with actionable
  field-level errors.
- Starter scenario set in `scenarios/`:
  - `sim_baseline.json`
  - `dropped_frames.json`
  - `trigger_roi.json`
- Run contract schema (`RunConfig`, `RunInfo`) with JSON serialization.
- Artifact writer for `<out>/run.json`.
- Event contract and append-only JSONL writer for `<out>/events.jsonl`.
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
  - writes `<out>/metrics.csv` and `<out>/metrics.json`
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
  - Catch2 core unit tests for schema/event JSON serialization (when available).

## Not Implemented Yet

- Scenario schema expansion (current validator covers core fields and
  constraints; deeper domain-specific rules can be added).
- Full metrics suite completion (disconnect-specific metrics beyond current FPS+drop+jitter timing).
- Baseline comparison and diff artifact outputs.
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

Expected files per run in the chosen output directory:
- `<out-dir>/run.json`
- `<out-dir>/events.jsonl`
- `<out-dir>/metrics.csv`
- `<out-dir>/metrics.json`

If you want to author new scenarios, follow `docs/scenario_schema.md`.

## Output Contracts

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
- `tests/README.md`
- `scenarios/README.md`

Most `src/` and `tests/` subfolders also include focused `README.md` files.

## Near-Term Roadmap

- Expand scenario schema checks and OAAT-specific validation depth.
- Expand metrics beyond current FPS+drop+jitter coverage (disconnect windows).
- Add baseline comparison/diff artifacts.
- Start agent experiment loop (change one variable at a time).
- Generate engineer packet output (repro steps, evidence, likely cause, next steps).
