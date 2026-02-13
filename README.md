# LabOps Agent

C++-first autopilot lab assistant for repeatable camera testing and faster triage.

## What This Project Is

LabOps Agent runs camera scenarios the same way every time, captures evidence
automatically, and prepares the foundation for AI-assisted root-cause
isolation.

Simple flow:
- You run a scenario.
- LabOps executes the backend with deterministic controls.
- It records `run.json` and `events.jsonl`.
- Tests verify contracts and determinism so behavior stays reproducible.

## Current Status

The repo currently has a working CLI, deterministic sim backend, artifact/event
output pipeline, and CI-validated tests. The agent diagnosis loop and metrics
stack are planned next.

## Implemented Today

- Project/build foundation with CMake.
- Cross-platform CI (`ubuntu`, `macos`, `windows`) with `ctest`.
- CLI commands:
  - `labops version`
  - `labops validate <scenario.json>`
  - `labops run <scenario.json> [--out <dir>]`
- Scenario loader + schema validation in `labops validate` with actionable
  field-level errors.
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
  - Metrics writer smoke test (`metrics.csv` + `metrics.json`).
  - Jitter injection smoke test.
  - Drop injection smoke test.
  - Determinism golden smoke test (same seed => same first K normalized events).
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

### 3) Create a scenario

```bash
cat > /tmp/labops-scenario.json <<'EOF'
{
  "name": "smoke",
  "duration_ms": 1200,
  "fps": 25,
  "jitter_us": 350,
  "seed": 777,
  "frame_size_bytes": 4096,
  "drop_every_n": 4,
  "drop_percent": 15,
  "burst_drop": 2,
  "reorder": 3
}
EOF
```

### 4) Validate scenario path/preflight

```bash
./build/labops validate /tmp/labops-scenario.json
```

### 5) Run and emit artifacts

```bash
./build/labops run /tmp/labops-scenario.json --out out/
```

Expected files:
- `out/run.json`
- `out/events.jsonl`
- `out/metrics.csv`
- `out/metrics.json`

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

- Add strict scenario schema + validation errors.
- Expand metrics beyond current FPS+drop+jitter coverage (disconnect windows).
- Add baseline comparison/diff artifacts.
- Start agent experiment loop (change one variable at a time).
- Generate engineer packet output (repro steps, evidence, likely cause, next steps).
