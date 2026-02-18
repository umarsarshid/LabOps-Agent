# LabOps Agent Handoff

This file helps the next coding agent continue work without re-discovery.

## Project Purpose

`labops` is a C++ CLI for repeatable camera test runs. Current capabilities:
- scenario validation
- deterministic sim streaming with fault injection
- run artifact generation (`scenario.json`, `run.json`, `events.jsonl`)
- metrics generation (`metrics.csv`, `metrics.json`)
- one-page run summary generation (`summary.md`)
- host system snapshot generation (`hostprobe.json`)
- NIC raw command evidence (`nic_*.txt`) + parsed highlights in host probe
  (including MTU/link hints when available)
- optional identifier redaction in host evidence via `--redact`
- scenario-level `netem_profile` references validated against
  `tools/netem_profiles/*.json` presets (definition-only stage)
- optional manual netem apply/show/teardown command suggestions in
  `summary.md` when `netem_profile` is configured
- optional Linux netem execution path (`--apply-netem --netem-iface <iface>`)
  with guaranteed teardown attempt on exit after successful apply
- agent experiment-state model plus `agent_state.json` serializer/writer
- in-process `ExperimentRunner` foundation for baseline + one-variant flow
- symptom-driven playbook framework for ordered knob selection
- one-variable-at-a-time scenario variant generator (`out/agent_runs`)
- deterministic stop-condition evaluator (reason + explanation)
- engineer packet generator (`engineer_packet.md` with evidence links)
- bundle manifest generation (`bundle_manifest.json`)
- optional support bundle zip (`--zip`)

Long-term goal:
- autonomous triage loop that changes one variable at a time and ships an
  engineer packet with repro steps, evidence, likely cause, and next actions.

## Current Snapshot (as of February 18, 2026)

- Latest commit before current work: `635bf25` (`chore: commit summary and update agent handoff policy`)
- Milestones completed:
  - Milestone 0: repo/build/style/CI foundation
  - Milestone 1: CLI skeleton + output contracts
  - Milestone 2: sim backend + deterministic/fault-injected stream runs
  - Milestone 3: metrics (fps/drop/jitter) + metrics artifacts
  - Milestone 4: scenario schema, loader/validator, scenario->backend apply
  - Milestone 5: bundle layout, manifest, optional zip, bundle docs
  - Milestone 6: baseline capture + compare diff outputs + threshold pass/fail
  - Milestone 7: done (hostprobe + NIC hints + redaction)
  - Milestone 8: done (netem profiles, suggestions, guarded execution)
  - Milestone 9: done (agent-mode foundation + packet/report flow)
  - Milestone R0-R8: done (real backend build gates, selection, params, frame loop, transport counters/anomalies)
  - Milestone R9: in progress (robustness; reconnect policy now implemented)
- Latest known test status:
  - `ctest --test-dir build --output-on-failure` passing (`62/62`)

## Confirmed Working Commands

Build/test:
- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

CLI:
- `./build/labops version`
- `./build/labops validate scenarios/sim_baseline.json`
- `./build/labops run scenarios/sim_baseline.json --out out/`
- `./build/labops run scenarios/sim_baseline.json --out out/ --zip`
- `./build/labops run scenarios/sim_baseline.json --out out-redacted/ --redact`
- `./build/labops validate scenarios/trigger_roi.json`

Expected run outputs:
- `<out>/<run_id>/scenario.json`
- `<out>/<run_id>/hostprobe.json`
- `<out>/<run_id>/nic_*.txt`
- `<out>/<run_id>/run.json`
- `<out>/<run_id>/events.jsonl`
- `<out>/<run_id>/metrics.csv`
- `<out>/<run_id>/metrics.json`
- `<out>/<run_id>/summary.md`
- `<out>/<run_id>/bundle_manifest.json`
- optional `<out>/<run_id>.zip`

## User Workflow Requirements (Important)

Follow these exactly when continuing commit-by-commit delivery:

1. Flow per task: `implementation -> test/verify -> commit`
2. Do not include commit numbers inside git commit messages.
3. In chat, keep it plain language and include:
   - what was implemented
   - how to verify
   - commit summary
4. Include the "why" for each implemented change.
5. Add strong senior-level code comments in new/updated files when helpful.
6. Add/maintain README files in subfolders to explain what is there, why it
   exists, and how it connects to the project.
7. Keep detailed, granular writeups in `SUMMARY.md`, overwrite it each time.
8. `SUMMARY.md` must be committed with each commit.

## Core Files To Read First

- `README.md`
- `docs/triage_bundle_spec.md`
- `docs/scenario_schema.md`
- `SUMMARY.md` (latest local implementation detail log)

## Repo Notes

- There is currently no root `.gitignore`; local dirs like `build/` and `out/`
  may appear untracked.
- `SUMMARY.md` is a tracked artifact and should be updated/committed each time.
- Most `src/`, `docs/`, `tests/`, and `scenarios/` subfolders include local
  `README.md` files with module-level context.

## Known Environment Quirk

Commands may print this startup noise from `.zshenv`:
- `/Users/umararshid/.zshenv:3: parse error near 'JAVA_HOME=...'`

Commands still run; it is a shell startup warning, not a LabOps failure.

## Next Likely Work

Use the user's explicit next commit request first. If no explicit task is
provided, likely follow-on work is:
- scenario-aware redaction expansion (if future security/privacy requirements add more fields)
- netem execution harness (apply/teardown orchestration instead of manual-only suggestions)
- richer netem status evidence in artifacts/events (applied/teardown outcomes)
- agent experiment planner/runner (OAAT isolation loop) on top of
  `ExperimentState` + `agent_state.json` + `ExperimentRunner` + symptom playbooks
  + generated OAAT variants + deterministic stop conditions + engineer packets
- engineer packet generation
- hardware SDK backend implementation behind `ICameraBackend`
