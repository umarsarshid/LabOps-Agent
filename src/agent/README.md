# src/agent

`src/agent/` contains the experiment-planning state and (next) orchestration logic.

## Why this folder exists

This module is the "autopilot engineer" layer. It tracks what the system
thinks might be wrong, what variables were already changed, and what each test
result showed. That state is the backbone for explainable one-change-at-a-time
triage.

## What is implemented now

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

## How it connects to the project

- Upstream: consumes run/scenario/baseline context.
- Downstream: feeds report generation and experiment continuation.
- Bundle impact: `agent_state.json` is the seed artifact for future engineer
  packets and replayable triage sessions.
