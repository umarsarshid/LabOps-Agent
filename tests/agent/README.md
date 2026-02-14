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
- `experiment_runner_failure_smoke.cpp`
  - Uses a missing variant scenario path.
  - Verifies `ExperimentRunner` fails fast with a clear error message.
  - Confirms no baseline/variant run starts when preflight validation fails.
- `playbook_selection_smoke.cpp`
  - Verifies symptom input resolves to the right playbook.
  - Verifies ordered knob list for dropped-frame triage.
  - Verifies unknown symptoms fail with actionable error text.
- `oaat_variant_generator_smoke.cpp`
  - Generates one-knob scenario variants from a base scenario.
  - Verifies default output location is `out/agent_runs`.
  - Verifies all dropped-frame playbook knobs produce persisted variant files.
- `stop_conditions_smoke.cpp`
  - Verifies deterministic stop evaluation.
  - Verifies each stop reason path:
    - max runs
    - single-variable flip
    - confidence threshold
    - stable repro rate
  - Verifies human-readable explanation text is emitted.
