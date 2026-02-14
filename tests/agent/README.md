# tests/agent

`tests/agent/` validates the agent-state contracts.

## Why this folder exists

Agent behavior will become more autonomous over time. Contract tests here keep
core state serialization stable so future planner/runner changes do not break
artifact consumers.

## Current test coverage

- `agent_state_writer_smoke.cpp`
  - Builds a representative experiment state.
  - Writes `agent_state.json`.
  - Verifies required model sections and key fields are present.
- `experiment_runner_smoke.cpp`
  - Runs baseline + one variant automatically via `ExperimentRunner`.
  - Confirms both runs produce expected bundle artifacts.
  - Verifies execution happens in-process through shared runner contracts.
