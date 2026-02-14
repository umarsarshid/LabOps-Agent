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
