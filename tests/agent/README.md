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
- `engineer_packet_writer_smoke.cpp`
  - Generates `engineer_packet.md` from state, attempts, and evidence links.
  - Verifies required packet sections are present.
  - Verifies exact artifact and diff paths are embedded in the packet.
  - Verifies citation wording links hypothesis conclusions to metric + event
    evidence in plain language.
- `agent_triage_integration_smoke.cpp`
  - Runs a seeded end-to-end triage flow on sim:
    - generate OAAT variants
    - execute baseline + variants
    - compare variant metrics to baseline
    - evaluate deterministic stop conditions
    - emit `agent_state.json` and `engineer_packet.md`
  - Verifies the agent stops on single-variable isolation and produces packet
    artifacts with evidence links.
