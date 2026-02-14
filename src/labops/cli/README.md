# src/labops/cli

This folder contains CLI routing and command dispatch behavior.

## Why this folder exists

As commands grow (`run`, `validate`, `version`, and later `bundle`, `agent`, `baseline`), command wiring needs a focused home so `main` stays minimal and behavior remains testable.

## Current responsibilities

- Map argv input to known subcommands.
- Enforce usage errors consistently.
- Preserve stable exit-code semantics for automation.
- Print deterministic command outputs.
- Parse run artifact options (`--out <dir>`, `--zip`, `--redact`,
  `--apply-netem`, `--netem-iface`, `--apply-netem-force`).
- Parse baseline capture command contracts (`baseline capture <scenario.json>
  [--redact] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`).
- Parse compare command contracts (`compare --baseline ... --run ... [--out ...]`).
- Route scenario validation through schema loader with actionable errors.
- Validate optional `netem_profile` references against
  `tools/netem_profiles/<profile>.json`.
- Apply scenario settings to backend params and emit `CONFIG_APPLIED` audit
  events.
- Execute sim backend run lifecycle and emit stream trace events.
- Compute and write run metrics (`metrics.csv` + `metrics.json`) for FPS,
  drop, and timing/jitter reporting.
- Generate one-page `summary.md` per run with pass/fail, key metrics, and top
  anomalies for quick human triage, including optional manual netem
  apply/show/teardown suggestions when `netem_profile` is configured.
- Optionally apply Linux netem profile impairments when explicitly requested
  (`--apply-netem`) and always teardown via scope guard on exit.
- Evaluate scenario thresholds (FPS/drop/timing) against computed metrics and
  return non-zero when thresholds fail.
- Emit standardized per-run bundles under `<out>/<run_id>/` including
  `scenario.json`, `run.json`, `events.jsonl`, metrics artifacts,
  `summary.md`, `hostprobe.json` (with parsed NIC MTU/link hints when available),
  platform NIC raw command outputs (`nic_*.txt`),
  and `bundle_manifest.json`
  (artifact list + hashes).
- Optionally emit support bundle zip archives via `--zip` at
  `<out>/<run_id>.zip`.
- Optionally redact obvious host/user identifiers in `hostprobe.json` and
  raw NIC outputs (`nic_*.txt`) via `--redact`.
- Emit scenario baseline captures under `baselines/<scenario_id>/` with
  `metrics.csv` + `metrics.json` for release-style comparison workflows.
- Compare baseline and run metric artifacts to emit `diff.json` + `diff.md`
  with per-metric deltas.
- Expose shared in-process run execution (`ExecuteScenarioRun`) so agent-mode
  can reuse the exact scenario pipeline without shelling out.

## Design intent

The router is intentionally explicit rather than clever. Early explicit branching is easier to audit, easier to test, and safer for a hardware-focused team where reproducibility matters more than abstraction density.

## Connection to the project

This is where reproducibility starts. If CLI contracts are ambiguous, every downstream test run and triage report becomes harder to trust.
