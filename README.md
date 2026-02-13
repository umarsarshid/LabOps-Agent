# LabOps Agent

C++-first autopilot lab assistant for repeatable camera testing and triage.

## What This Project Is

LabOps Agent is being built to run camera tests in a consistent way, capture evidence automatically, and speed up root-cause isolation.

Simple version:
- You run a scenario.
- LabOps records run metadata and timeline events.
- It writes artifacts in a predictable format.
- The system is structured so metrics, baselines, and agent-led diagnosis can be added incrementally.

## What Is Implemented Today

- Build and CLI foundation.
- Cross-platform CI build matrix (`ubuntu`, `macos`, `windows`).
- CI test execution via `ctest`.
- CLI subcommands:
  - `labops version`
  - `labops validate <scenario.json>`
  - `labops run <scenario.json> [--out <dir>]`
- Run schema contracts (`RunConfig`, `RunInfo`) with JSON serialization.
- Artifact writer for `run.json`.
- Event model (`EventType`, `Event`) with JSON serialization.
- Append-only `events.jsonl` writer (one JSON object per line).
- `labops run` now emits:
  - `<out>/run.json`
  - `<out>/events.jsonl` (sample `run_started` event)
- Smoke tests for schema, artifacts, and events.
- Catch2 integration for core unit tests (schema JSON + event JSON).

## What Is Not Implemented Yet

- Full scenario schema parsing/validation rules (beyond file preflight checks).
- Real execution runtime for camera streaming scenarios.
- Sim backend behavior modeling (drops/jitter/disconnect logic).
- Vendor SDK backend integration (only stubs/contracts planned).
- Metrics engine (FPS, jitter, drops, disconnect windows).
- Baseline comparison and diff reporting.
- Agent experiment loop (change one variable at a time, isolate failures).
- Final engineer packet assembly flow.

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

### 3) Create a minimal scenario file for local checks

```bash
cat > /tmp/labops-scenario.json <<'EOF'
{
  "name": "smoke"
}
EOF
```

### 4) Validate a scenario file

```bash
./build/labops validate /tmp/labops-scenario.json
```

Note: scenario preflight currently expects an existing non-empty `.json` file.

### 5) Run and emit artifacts

```bash
./build/labops run /tmp/labops-scenario.json --out out/
```

Expected files:
- `out/run.json`
- `out/events.jsonl`

## Output Contracts

### run.json

Current run metadata includes:
- `run_id`
- `config.scenario_id`
- `config.backend`
- `config.seed`
- `config.duration_ms`
- `timestamps.created_at_utc`
- `timestamps.started_at_utc`
- `timestamps.finished_at_utc`

### events.jsonl

- Append-only log.
- One JSON event per line.
- Current runner emits a sample `run_started` event with payload fields such as `run_id`, `scenario_id`, and `backend`.

## Testing

### Smoke tests (always available)

```bash
ctest --test-dir build --output-on-failure
```

### Catch2 unit tests

Catch2-backed tests live under `tests/core/` and cover schema/event JSON contracts.

Local environments without internet may skip Catch2 tests if Catch2 is not installed.

To allow CMake to fetch Catch2 when network access is available:

```bash
cmake -S . -B build -DLABOPS_FETCH_CATCH2=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## CI

GitHub Actions workflow:
- Builds on `ubuntu-latest`, `macos-latest`, `windows-latest`.
- Runs `ctest` in CI.
- Enables Catch2 fetching in CI configure step.

## Repository Guide

Detailed folder-level ownership docs are in:
- `src/README.md`
- `docs/README.md`
- `tests/README.md`
- `scenarios/README.md`

Each major subfolder under `src/` and `tests/` also has its own `README.md`.

## Near-Term Roadmap

- Implement strict scenario schema loader and validation errors.
- Implement deterministic sim run lifecycle and richer event emission.
- Compute metrics from events.
- Add baseline comparison and report outputs.
- Add initial agent loop for controlled experiment iteration.
