# src/agent

`src/agent/` contains the experiment-planning state and (next) orchestration logic.

## Why this folder exists

This module is the "autopilot engineer" layer. It tracks what the system
thinks might be wrong, what variables were already changed, and what each test
result showed. That state is the backbone for explainable one-change-at-a-time
triage.

## What is implemented now

- `experiment_runner.hpp` / `experiment_runner.cpp`
  - In-process `ExperimentRunner` for milestone-9 bootstrap.
  - Runs baseline capture + one variant automatically.
  - Reuses the same internal scenario runner used by CLI (`ExecuteScenarioRun`)
    so behavior stays aligned with standard run contracts.
  - Performs preflight scenario-path validation so failures are explicit and
    actionable before any run artifacts are produced.
- `playbook.hpp` / `playbook.cpp`
  - Symptom-driven playbook framework.
  - Maps a symptom string to an ordered list of knobs to try.
  - Current built-in playbook:
    - `dropped_frames` -> `packet_delay_ms`, `fps`, `roi_enabled`,
      `reorder_percent`, `loss_percent`
- `experiment_state.hpp` / `experiment_state.cpp`
  - Canonical `ExperimentState` model.
  - Structured lists for:
    - hypotheses
    - tested variables
    - results table
  - Stable JSON serialization for checkpointing and downstream tooling.
- `state_writer.hpp` / `state_writer.cpp`
  - Writes `agent_state.json` into an output directory.

## Why this implementation is useful right now

Even before full planner/runner logic lands, we can persist the agent's working
memory in a consistent file contract (`agent_state.json`). This prevents
"stateless" debugging and gives future automation and humans the same source of
truth.

We now also have a first orchestration primitive: baseline + one variant in a
single in-process flow, without shelling out to CLI subprocesses.

The new playbook layer gives the runner a deterministic "what to try next"
framework per symptom, which is required for explainable one-knob-at-a-time
triage.

## How it connects to the project

- Upstream: consumes run/scenario/baseline context.
- Downstream: feeds report generation and experiment continuation.
- Bundle impact: `agent_state.json` is the seed artifact for future engineer
  packets and replayable triage sessions.
