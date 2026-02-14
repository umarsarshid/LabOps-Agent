# ProjectDesc: LabOps Agent

## 1) Project Purpose

LabOps Agent is a C++ CLI system for camera-lab triage automation.

Core goal:
- make test execution repeatable,
- collect evidence automatically,
- reduce guesswork during root-cause analysis,
- and produce handoff-ready triage packets.

The system is designed to work without physical camera hardware first (via deterministic sim backend), while keeping an interface boundary ready for real SDK integration.

## 2) Engineering Principles

- Determinism first: same scenario + same seed should reproduce the same behavior.
- Evidence over opinion: every conclusion links back to concrete artifacts.
- Contract-driven outputs: stable file shapes and exit codes for CI/tooling.
- Safe operations: optional netem controls are explicit and teardown is guarded.
- Human handoff quality: summarize what changed, what failed, what is ruled out.

## 3) End-to-End Workflow

### A) Validate

`labops validate <scenario.json>`

- Parses scenario JSON.
- Enforces schema/value constraints.
- Returns actionable errors with field paths.

### B) Run

`labops run <scenario.json> --out <dir> [--zip] [--redact]`

- Loads scenario and applies config to backend.
- Streams frames from backend (sim today).
- Emits events and metrics.
- Writes triage bundle artifacts.
- Evaluates thresholds and returns deterministic exit codes.

Soak mode extension:

`labops run <scenario.json> --soak --checkpoint-interval-ms <ms> [--soak-stop-file <path>] [--resume <checkpoint.json>]`

- Splits long runs into checkpointed chunks.
- Persists periodic progress snapshots and frame cache.
- Supports safe pause at checkpoint boundaries and deterministic resume without
  losing already captured evidence.

### C) Baseline Capture

`labops baseline capture <scenario.json>`

- Runs scenario in baseline mode.
- Writes stable baseline bundle under `baselines/<scenario_id>/`.

### D) Compare

`labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv>`

- Computes metric deltas.
- Writes `diff.json` and `diff.md` for machine/human review.

### E) Agent Triage Foundations

- Track hypotheses/experiments in `agent_state.json`.
- Generate OAAT variants (one knob changed per scenario variant).
- Evaluate deterministic stop conditions.
- Generate `engineer_packet.md` with ranked hypotheses and evidence citations.

## 4) Repository Architecture

Top-level:

- `src/labops/`: CLI entrypoints and routing.
- `src/core/`: core schemas, logging, error/exit contracts.
- `src/events/`: event model + JSONL writer.
- `src/metrics/`: FPS/drop/jitter metrics and anomaly heuristics.
- `src/artifacts/`: run outputs (`run.json`, metrics writers, summary/report, diffs, manifest, zip).
- `src/scenarios/`: scenario validation/loading.
- `src/backends/sim/`: deterministic fake camera backend.
- `src/backends/sdk_stub/`: real-backend interface/stub without proprietary SDK.
- `src/hostprobe/`: host + NIC evidence capture and redaction.
- `src/agent/`: experiment state, playbooks, variant generation, stop conditions, engineer packet writer.

Support:

- `scenarios/`: starter scenario fixtures.
- `docs/`: bundle spec, scenario schema, release checklist, playbooks.
- `tests/`: smoke/integration contract coverage.
- `tools/netem_profiles/`: netem preset definitions.

## 5) CLI Surface and Exit Behavior

Main commands:

- `labops version`
- `labops validate <scenario>`
- `labops run <scenario> [flags]`
- `labops baseline capture <scenario> [flags]`
- `labops compare --baseline ... --run ... [--out ...]`

Important run flags:

- `--out <dir>`
- `--zip`
- `--redact`
- `--log-level <debug|info|warn|error>`
- Linux netem path:
  - `--apply-netem --netem-iface <iface> [--apply-netem-force]`

Exit code contract (automation-safe):

- `0`: success
- `1`: generic runtime failure
- `2`: usage error
- `10`: scenario schema invalid
- `20`: backend connect failure
- `30`: thresholds failed

## 6) Scenario Model (Current)

Scenario contracts currently support:

- identity/metadata (`scenario_id`, description, tags)
- duration (`duration_ms`)
- camera parameters (fps, trigger mode, ROI/pixel-format related fields)
- sim fault knobs (seed, jitter, periodic/percent/burst drop, reorder)
- thresholds (min/max metric gates)
- optional netem profile references

Full field-level contract and validation semantics are in `docs/scenario_schema.md`.

## 7) Deterministic Sim Backend

`src/backends/sim` provides reproducible frame generation:

- frame stream with timestamp, id, size, dropped marker
- configurable FPS and jitter
- deterministic seeded behavior
- fault injection controls:
  - `drop_every_n`
  - `drop_percent`
  - `burst_drop`
  - `reorder`

Why it matters:
- enables CI and local triage workflow without requiring physical camera labs.

## 8) Events and Metrics

### Event stream (`events.jsonl`)

Current event types:

- `CONFIG_APPLIED`
- `STREAM_STARTED`
- `FRAME_RECEIVED`
- `FRAME_DROPPED`
- `STREAM_STOPPED`

### Metrics

Generated metrics include:

- average FPS
- rolling FPS samples
- dropped frames and drop-rate percent
- inter-frame interval stats (`min`, `avg`, `p95`)
- inter-frame jitter stats (`min`, `avg`, `p95`)

Anomaly heuristics currently include:

- resend spike
- jitter cliff
- periodic stall

## 9) Artifact and Bundle Contracts

Per-run bundle layout:

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
- optional soak-mode state artifacts:
  - `soak_checkpoint.json`
  - `checkpoints/checkpoint_*.json`
  - `soak_frames.jsonl`

Optional archive:

- `<out>/<run_id>.zip` with `--zip`

Comparison artifacts:

- `diff.json`
- `diff.md`

Bundle spec reference:

- `docs/triage_bundle_spec.md`

## 10) Host Evidence and Redaction

Host evidence captures:

- OS/CPU/RAM/uptime/load snapshot
- NIC parsed highlights (including MTU/link hints when available)
- raw platform-specific NIC command output (`nic_*.txt`)

Redaction mode (`--redact`):

- strips obvious hostname/username identifiers from host outputs.

## 11) Netem Harness Status

Implemented:

- netem profile definitions and scenario references
- manual command suggestions in summary
- optional guarded Linux execution path when explicitly requested
- teardown attempt on exit after successful apply

Operational intent:
- impairments are explicit, reversible, and auditable in artifacts.

## 12) Agent Mode (Current State)

Implemented foundations in `src/agent/`:

- `ExperimentState` model + JSON writer (`agent_state.json`)
- `ExperimentRunner` baseline + one variant in-process flow
- symptom playbook selector
- OAAT variant generation (`out/agent_runs`, manifest)
- deterministic stop-condition evaluator
- engineer packet generation (`engineer_packet.md`)
- evidence citation support in packet text:
  - per hypothesis/run sentence ties conclusion to metric + event
  - optional detailed citation notes and source links

This is already useful for structured, explainable triage handoff. Full autonomous experiment loops are the next layer.

## 13) Testing Strategy

Test coverage is primarily smoke + integration contracts:

- serialization contracts
- scenario validation behavior
- run/bundle artifact contracts
- deterministic sim behavior
- metrics and anomaly behavior
- baseline/compare workflows
- threshold exit semantics
- hostprobe redaction behavior
- agent outputs and triage integration flow

Standard command:

```bash
ctest --test-dir tmp/build --output-on-failure
```

## 14) CI and Style

CI matrix:

- Ubuntu
- macOS
- Windows

Formatting:

- clang-format required major: 21
- helper script: `tools/clang_format.sh`

## 15) Real Backend Readiness

`src/backends/sdk_stub/` and backend interfaces are in place so a proprietary camera SDK can be integrated later without rewriting CLI/scenario/artifact layers.

Expected integration path:

1. implement backend interface methods for SDK session lifecycle and frame pull.
2. map scenario params to SDK configuration calls.
3. preserve existing event/metrics/artifact contracts.
4. validate against existing smoke/integration contracts where applicable.

## 16) Current Gaps and Next Steps

Still maturing:

- deeper agent planning and multi-iteration autonomous triage loop
- richer hypothesis confidence modeling
- real hardware SDK backend implementation
- broader metric families for hardware-specific transport/trigger behavior

The foundation is intentionally strong: deterministic execution, evidence bundles, baseline comparison, and explainable packet output are already operational.
