# LabOps Agent Handoff

This file is the operational guide for the next coding agent.
Use it to continue work quickly without re-discovery.

## 1) What This Project Is

`labops` is a C++ CLI for repeatable camera testing and triage.

At a high level it can:
- validate a scenario
- run a stream test (sim backend now, real backend integration path in progress)
- collect evidence artifacts (events, metrics, config, host/network evidence)
- compare runs against baselines
- run rule-based agent triage flows (OAAT style)
- generate engineer-facing packets/reports

Long-term goal:
- autonomous triage loop that changes one variable at a time and produces a
  high-quality engineer packet with repro steps, evidence, likely cause, and
  next actions.

## 2) Current Status Snapshot (as of Feb 18, 2026)

Recent commits:
- `4465235` `docs(agent): tighten commit style rules`
- `c242372` `chore(real): fix clang formatting`
- `089fbdd` `chore(real): harden error mapping and actionable messages`
- `61e4a0a` `feat(real): add optional sdk log capture`

Milestone maturity:
- Milestone 0-9: completed
- Real-backend track (R0-R8): completed foundation and runtime wiring
- Real-backend robustness (R9): in progress

Latest known suite status:
- `ctest --test-dir build --output-on-failure` passing (`64/64`)

## 3) Architecture Map (Where Things Live)

- `src/labops/cli/`
  - command routing (`run`, `validate`, `compare`, `baseline`, `kb`, etc.)
  - high-level run orchestration
- `src/core/`
  - schema contracts, exit codes, logging primitives
- `src/events/`
  - event model + JSONL writing + anomaly events
- `src/metrics/`
  - FPS/drop/jitter computations and anomaly heuristics
- `src/artifacts/`
  - all run outputs (`run.json`, `metrics.*`, summary/report files, bundle manifest)
- `src/scenarios/`
  - schema validation and scenario parsing/compatibility
- `src/backends/sim/`
  - deterministic simulation backend + fault injection
- `src/backends/real_sdk/`
  - real-backend integration path (non-proprietary scaffolding)
  - param bridge, selector logic, stream/session lifecycle, transport counters
  - stable error mapping (`REAL_*` codes)
- `src/backends/sdk_stub/`
  - fallback backend behavior when real SDK is unavailable
- `src/hostprobe/`
  - host and NIC evidence collection + redaction path
- `src/agent/`
  - experiment state, playbooks, variant generation, stop conditions, packet generation
- `src/labops/soak/`
  - checkpoint and resume persistence support

## 4) Backend Reality (Important)

Supported run backends in scenario:
- `sim`
- `real_stub`

Real backend notes:
- Build flag controls real enablement: `LABOPS_ENABLE_REAL_BACKEND`
- Real discovery and selector resolution exist
- Real session/frame loop path exists in OSS-compatible form
- Proprietary SDK-specific wiring is intentionally not committed here

Interpretation:
- The system is already useful for repeatable workflow validation and triage-flow
  behavior in sim and scaffolded real paths.
- Full production hardware behavior depends on vendor SDK integration in
  environment-specific code.

## 5) Artifact Contract (Run Output)

Typical bundle path:
- `<out>/<run_id>/...`

Core artifacts:
- `scenario.json`
- `run.json`
- `events.jsonl`
- `metrics.csv`
- `metrics.json`
- `summary.md`
- `report.html`
- `bundle_manifest.json`

Context artifacts:
- `hostprobe.json`
- `nic_*.txt`
- `config_verify.json` (real apply/readback path)
- `camera_config.json` (real config dump)
- `config_report.md` (human-readable config status table)
- `sdk_log.txt` (if `--sdk-log`)
- optional `<run_id>.zip` (if `--zip`)

Soak-mode artifacts:
- `soak_checkpoint.json`
- `checkpoints/checkpoint_*.json`
- `soak_frames.jsonl`

## 6) Command Cookbook

Build/test:
- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

Style gate:
- `bash tools/clang_format.sh --check`

Common CLI:
- `./build/labops version`
- `./build/labops list-backends`
- `./build/labops list-devices --backend real`
- `./build/labops validate scenarios/sim_baseline.json`
- `./build/labops run scenarios/sim_baseline.json --out tmp/runs`
- `./build/labops baseline capture scenarios/sim_baseline.json`
- `./build/labops compare --baseline baselines/sim_baseline --run tmp/runs/<run_id>`
- `./build/labops kb draft --run tmp/runs/<run_id>`

## 7) User Workflow Rules (Do Not Break)

Follow this flow every task:
1. implementation
2. test/verify
3. commit

Communication requirements:
- plain language in chat
- always include:
  - what was implemented
  - how to verify
  - commit summary
- include the "why" for each implemented change

Codebase hygiene requirements:
- add strong senior-level comments where useful
- maintain per-folder README context docs when touching modules
- keep commit messages short and clean
  - preferred style: `type(scope): intent`
  - avoid long multi-paragraph commit bodies unless user asks

Summary requirements:
- overwrite `SUMMARY.md` each commit with detailed notes
- `SUMMARY.md` is tracked and should be committed

Commit-message constraints:
- do **not** include milestone/commit numbers in git subject lines

## 8) Definition of Done Checklist (Per Commit)

Before commit:
- `bash tools/clang_format.sh --check`
- build passes: `cmake --build build`
- relevant targeted tests pass
- for risky/core changes: run full `ctest --test-dir build --output-on-failure`
- update `SUMMARY.md`
- verify only intended files are staged

After commit:
- confirm `git status --short` is clean except expected local dirs (like `build/`)
- provide short plain-language summary + verify commands + commit hash

## 9) Testing Surface (Useful Targets)

Useful focused tests:
- real error mapping: `real_error_mapper_smoke`
- reconnect behavior: `run_reconnect_policy_smoke`
- device selection: `run_device_selector_resolution_smoke`
- backend connect failure contract: `run_backend_connect_failure_smoke`
- list-devices behavior: `list_devices_real_backend_smoke`
- apply-mode event contract: `real_apply_mode_events_smoke`

When touching router/backends broadly, run full suite.

## 10) Known Environment Quirk

Shell startup may print:
- `/Users/umararshid/.zshenv:3: parse error near 'JAVA_HOME=...'`

This is environmental noise; commands still run.

## 11) What To Read First (When Picking Up Work)

1. `README.md`
2. `ProjectDesc.md`
3. `docs/triage_bundle_spec.md`
4. `docs/scenario_schema.md`
5. `src/labops/cli/README.md`
6. `src/backends/real_sdk/README.md`
7. `SUMMARY.md`

## 12) Next Likely Work

Use explicit user request first.
If none is provided, likely next high-value items are:
- real backend disconnect/recovery hardening details
- richer transport anomaly evidence/events
- further real SDK mapping coverage and readback fidelity
- broader agent loop behavior (planning/execution/stopping confidence)
- additional bundle/report polish for engineering handoff quality

## 13) Guardrails

- Do not commit proprietary SDK files/binaries.
- Do not weaken stable output contracts without tests/docs updates.
- Do not change exit-code semantics casually (CI and tooling depend on them).
- Keep changes atomic and reviewable.
