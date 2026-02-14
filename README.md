# LabOps Agent

C++-first autopilot lab assistant for repeatable camera testing and faster triage handoff.

## TL;DR

`labops` runs deterministic scenario tests, captures evidence bundles, compares runs against baselines, and now includes an agent foundation that can produce an engineer packet with cited evidence.

## What You Can Do Today

- Validate scenarios with actionable errors.
- Run deterministic sim camera streams with fault injection.
- Capture full run bundles (`run.json`, `events.jsonl`, `metrics.*`, `summary.md`, `report.html`, host/NIC evidence).
- Capture baselines and compare runs (`diff.json`, `diff.md`).
- Enforce scenario thresholds with stable non-zero exit codes.
- Generate agent artifacts (`agent_state.json`, `engineer_packet.md`) with hypothesis evidence citations.
- Run long soak tests with periodic checkpoints and safe pause/resume (`soak_checkpoint.json`, `soak_frames.jsonl`).

## Quick Start

```bash
cmake -S . -B tmp/build
cmake --build tmp/build
```

```bash
./tmp/build/labops version
./tmp/build/labops validate scenarios/sim_baseline.json
./tmp/build/labops run scenarios/sim_baseline.json --out tmp/runs
```

Optional flows:

```bash
./tmp/build/labops baseline capture scenarios/sim_baseline.json
./tmp/build/labops compare --baseline baselines/sim_baseline --run tmp/runs/<run_id>
./tmp/build/labops run scenarios/sim_baseline.json --out tmp/runs --zip --redact
./tmp/build/labops run scenarios/sim_baseline.json --out tmp/runs --soak --checkpoint-interval-ms 60000
```

## Typical Triage Flow

1. Validate the target scenario.
2. Capture or reuse a known-good baseline.
3. Run the suspect scenario and collect a bundle.
4. Compare run metrics vs baseline.
5. Review `summary.md`, `report.html`, and event/metric evidence.
6. Use agent outputs (`agent_state.json`, `engineer_packet.md`) for handoff.

## Example Flow (Need -> Do -> Get)

What you need:
- A scenario file representing the issue (example: `scenarios/dropped_frames.json`)
- A baseline scenario (example: `scenarios/sim_baseline.json`)
- A built binary (`./tmp/build/labops`)
- An output folder (example: `tmp/runs_camera_team`)

What you do (stage by stage):

Stage 0: set working variables
```bash
LABOPS=./tmp/build/labops
SCENARIO=scenarios/dropped_frames.json
BASELINE_SCENARIO=scenarios/sim_baseline.json
OUT_ROOT=tmp/runs_camera_team
mkdir -p "$OUT_ROOT"
```

Stage 1: validate the issue scenario
```bash
"$LABOPS" validate "$SCENARIO"
```
Expected: `valid: scenarios/dropped_frames.json`

Stage 2: capture known-good baseline
```bash
"$LABOPS" baseline capture "$BASELINE_SCENARIO"
```
Expected baseline location: `baselines/sim_baseline/`

Stage 3: run the issue scenario and collect a bundle
```bash
"$LABOPS" run "$SCENARIO" --out "$OUT_ROOT"
```
Expected stdout includes:
- `run_id: run-...`
- `bundle: tmp/runs_camera_team/run-...`

Stage 4: locate the latest run folder and inspect core files
```bash
RUN_DIR="$(find "$OUT_ROOT" -maxdepth 1 -type d -name 'run-*' | sort | tail -n 1)"
ls -la "$RUN_DIR"
```

Stage 5: compare run against baseline
```bash
"$LABOPS" compare --baseline baselines/sim_baseline --run "$RUN_DIR"
```
Expected new files in `"$RUN_DIR"`:
- `diff.json`
- `diff.md`

Stage 6 (optional): long soak run with checkpoints
```bash
"$LABOPS" run "$SCENARIO" --out "$OUT_ROOT" --soak --checkpoint-interval-ms 60000
```
If paused and resumed:
```bash
"$LABOPS" run "$SCENARIO" --soak --resume "$RUN_DIR/soak_checkpoint.json"
```

What you get:
- A reproducible bundle in `"$RUN_DIR"` with `run.json`, `events.jsonl`, `metrics.csv`, `metrics.json`, `summary.md`, `report.html`, and `bundle_manifest.json`
- Host/network context evidence (`hostprobe.json`, `nic_*.txt`)
- Baseline-vs-run deltas (`diff.json`, `diff.md`)
- For soak mode: resumable progress (`soak_checkpoint.json`, `checkpoints/checkpoint_*.json`, `soak_frames.jsonl`)

## Real-Life Workflow (Scenario from a Camera Team)

If the camera is not directly connected to this system yet, this is still useful:

1. Engineer A observes real camera behavior and writes a clean scenario file.
2. Engineer A captures a baseline from that scenario once.
3. On later changes (settings, firmware, network), the team reruns and compares.
4. LabOps shows exactly what changed in metrics and events.
5. The team shares one evidence bundle instead of back-and-forth debugging.

In plain terms: this gives engineers a repeatable test harness and consistent
proof package, even before live camera SDK integration is wired in.

## Bundle Output (Per Run)

`<out>/<run_id>/`

- `scenario.json`
- `hostprobe.json`
- `nic_*.txt`
- `run.json`
- `events.jsonl`
- `metrics.csv`
- `metrics.json`
- `summary.md`
- `report.html`
- `bundle_manifest.json`
- optional `<out>/<run_id>.zip` when `--zip` is set
- optional soak-mode artifacts:
  - `soak_checkpoint.json`
  - `checkpoints/checkpoint_*.json`
  - `soak_frames.jsonl`

## Docs Map

- Detailed project description: `ProjectDesc.md`
- Bundle contract/spec: `docs/triage_bundle_spec.md`
- Scenario schema: `docs/scenario_schema.md`
- Release verification checklist: `docs/release_verification.md`
- Source/test module guides: `src/README.md`, `tests/README.md`, `scenarios/README.md`

## Dev Commands

Formatting:

```bash
CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check
```

Tests:

```bash
ctest --test-dir tmp/build --output-on-failure
```

## Status Snapshot

- Milestones 0-8: complete
- Milestone 9 (agent mode): in progress, core foundation implemented
- Milestone 10+: readiness/polish tracks remain

For full architecture, contracts, and implementation details, use `ProjectDesc.md`.
