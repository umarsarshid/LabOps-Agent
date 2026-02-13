# src/labops/cli

This folder contains CLI routing and command dispatch behavior.

## Why this folder exists

As commands grow (`run`, `validate`, `version`, and later `bundle`, `agent`, `baseline`), command wiring needs a focused home so `main` stays minimal and behavior remains testable.

## Current responsibilities

- Map argv input to known subcommands.
- Enforce usage errors consistently.
- Preserve stable exit-code semantics for automation.
- Print deterministic command outputs.
- Parse run artifact options (currently `--out <dir>`).
- Parse baseline capture command contracts (`baseline capture <scenario.json>`).
- Route scenario validation through schema loader with actionable errors.
- Apply scenario settings to backend params and emit `CONFIG_APPLIED` audit
  events.
- Execute sim backend run lifecycle and emit stream trace events.
- Compute and write run metrics (`metrics.csv` + `metrics.json`) for FPS,
  drop, and timing/jitter reporting.
- Emit standardized per-run bundles under `<out>/<run_id>/` including
  `scenario.json`, `run.json`, `events.jsonl`, metrics artifacts, and
  `bundle_manifest.json` (artifact list + hashes).
- Optionally emit support bundle zip archives via `--zip` at
  `<out>/<run_id>.zip`.
- Emit scenario baseline captures under `baselines/<scenario_id>/` with
  `metrics.csv` + `metrics.json` for release-style comparison workflows.

## Design intent

The router is intentionally explicit rather than clever. Early explicit branching is easier to audit, easier to test, and safer for a hardware-focused team where reproducibility matters more than abstraction density.

## Connection to the project

This is where reproducibility starts. If CLI contracts are ambiguous, every downstream test run and triage report becomes harder to trust.
