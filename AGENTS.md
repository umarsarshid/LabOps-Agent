# LabOps Agent Handoff

This file helps the next coding agent continue work without re-discovery.

## Project Purpose

`labops` is a C++ CLI for repeatable camera test runs. Current capabilities:
- scenario validation
- deterministic sim streaming with fault injection
- run artifact generation (`scenario.json`, `run.json`, `events.jsonl`)
- metrics generation (`metrics.csv`, `metrics.json`)
- one-page run summary generation (`summary.md`)
- bundle manifest generation (`bundle_manifest.json`)
- optional support bundle zip (`--zip`)

Long-term goal:
- autonomous triage loop that changes one variable at a time and ships an
  engineer packet with repro steps, evidence, likely cause, and next actions.

## Current Snapshot (as of February 14, 2026)

- Latest commit: `55937c5` (`feat(thresholds): enforce scenario pass/fail in run`)
- Milestones completed:
  - Milestone 0: repo/build/style/CI foundation
  - Milestone 1: CLI skeleton + output contracts
  - Milestone 2: sim backend + deterministic/fault-injected stream runs
  - Milestone 3: metrics (fps/drop/jitter) + metrics artifacts
  - Milestone 4: scenario schema, loader/validator, scenario->backend apply
  - Milestone 5: bundle layout, manifest, optional zip, bundle docs
  - Milestone 6: baseline capture + compare diff outputs + threshold pass/fail
- Milestone 7 status:
  - `0033` in progress: per-run `summary.md` one-page report
- Latest known test status: baseline/compare/threshold/run smoke suite passing.

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

Expected run outputs:
- `<out>/<run_id>/scenario.json`
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
8. `SUMMARY.md` must remain uncommitted.

## Core Files To Read First

- `README.md`
- `docs/triage_bundle_spec.md`
- `docs/scenario_schema.md`
- `SUMMARY.md` (latest local implementation detail log)

## Repo Notes

- There is currently no root `.gitignore`; local dirs like `build/` and `out/`
  may appear untracked.
- `SUMMARY.md` is intentionally untracked and should stay local.
- Most `src/`, `docs/`, `tests/`, and `scenarios/` subfolders include local
  `README.md` files with module-level context.

## Known Environment Quirk

Commands may print this startup noise from `.zshenv`:
- `/Users/umararshid/.zshenv:3: parse error near 'JAVA_HOME=...'`

Commands still run; it is a shell startup warning, not a LabOps failure.

## Next Likely Work

Use the user's explicit next commit request first. If no explicit task is
provided, likely follow-on work is:
- finish and commit `0033` (`summary.md` writer + docs/tests)
- agent experiment planner/runner (OAAT isolation loop)
- engineer packet generation
- hardware SDK backend implementation behind `ICameraBackend`
