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
  `--soak`, `--checkpoint-interval-ms <ms>`, `--soak-stop-file <path>`,
  `--resume <checkpoint.json>`, `--log-level <debug|info|warn|error>`,
  `--apply-netem`, `--netem-iface`, `--apply-netem-force`,
  `--device <selector>`).
- Parse baseline capture command contracts (`baseline capture <scenario.json>
  [--redact] [--device <selector>] [--log-level <debug|info|warn|error>] [--apply-netem
  --netem-iface <iface> [--apply-netem-force]]`).
- Parse compare command contracts (`compare --baseline ... --run ... [--out ...]`).
- Parse KB drafting contracts (`kb draft --run <run_folder> [--out <kb_draft.md>]`).
- Surface backend availability contracts (`list-backends`) with human-readable
  status reasons.
- Surface backend device-discovery contract (`list-devices --backend real`)
  including friendly `BACKEND_NOT_AVAILABLE` messaging when real backend is not
  active.
  - when enabled, prints normalized per-device identity fields
    (`model`, `serial`, `user_id`, `transport`, optional `ip`/`mac`,
    optional `firmware_version`/`sdk_version`) from real-backend descriptor
    mapping.
- Route scenario validation through schema loader with actionable errors.
- Parse and validate scenario-level `device_selector` plus CLI `--device`
  overrides; resolve selectors deterministically (serial/user_id with optional
  index tie-break) before backend connect so repeated runs target the same
  camera identity.
  - attach resolved identity/version metadata to `run.json` (`real_device`)
    so triage bundles capture exact hardware provenance.
- Validate optional `netem_profile` references against
  `tools/netem_profiles/<profile>.json`.
- Apply scenario settings to backend params and emit config-audit events:
  `CONFIG_APPLIED`, plus `CONFIG_UNSUPPORTED`/`CONFIG_ADJUSTED` for real
  backend apply-mode flows (`apply_mode: strict|best_effort`).
- For real-backend runs, emit `config_verify.json` with requested vs actual vs
  supported per-setting readback evidence after apply.
- Execute sim backend run lifecycle and emit stream trace events.
- On backend connect failures, still emit `run.json` (when bundle dir is
  already initialized) so early-failure runs preserve run metadata evidence.
- Compute and write run metrics (`metrics.csv` + `metrics.json`) for FPS,
  drop, and timing/jitter reporting.
- Generate one-page `summary.md` per run with pass/fail, key metrics, and top
  anomalies for quick human triage, including named heuristics (`resend spike`,
  `jitter cliff`, `periodic stall`) plus optional manual netem
  apply/show/teardown suggestions when `netem_profile` is configured.
- Generate static `report.html` per run for browser-based review with no JS,
  including plots-ready metric and delta tables.
- Optionally apply Linux netem profile impairments when explicitly requested
  (`--apply-netem`) and always teardown via scope guard on exit.
- Emit structured runtime logs with selectable severity and run-id context for
  artifact correlation.
- Evaluate scenario thresholds (FPS/drop/timing) against computed metrics and
  return non-zero when thresholds fail.
- Emit standardized per-run bundles under `<out>/<run_id>/` including
  `scenario.json`, `run.json`, `config_verify.json` (real backend),
  `events.jsonl`, metrics artifacts,
  `summary.md`, `report.html`, `hostprobe.json`
  (with parsed NIC MTU/link hints when available),
  platform NIC raw command outputs (`nic_*.txt`),
  and `bundle_manifest.json`
  (artifact list + hashes).
- Optionally emit support bundle zip archives via `--zip` at
  `<out>/<run_id>.zip`.
- Optionally redact obvious host/user identifiers in `hostprobe.json` and
  raw NIC outputs (`nic_*.txt`) via `--redact`.
- Optionally run long-duration soak mode with periodic checkpoints and safe
  pause/resume behavior:
  - `soak_checkpoint.json` + `checkpoints/checkpoint_*.json`
  - `soak_frames.jsonl` frame cache used for resume without evidence loss
  - checkpoint-boundary safe stop via signal (`Ctrl-C`) or `--soak-stop-file`
- Emit scenario baseline captures under `baselines/<scenario_id>/` with
  `metrics.csv` + `metrics.json` for release-style comparison workflows.
- Compare baseline and run metric artifacts to emit `diff.json` + `diff.md`
  with per-metric deltas.
- Generate `kb_draft.md` from `engineer_packet.md` so resolved investigations
  can be turned into review-ready knowledge-base drafts quickly.
- Expose shared in-process run execution (`ExecuteScenarioRun`) so agent-mode
  can reuse the exact scenario pipeline without shelling out.

Current exit-code contract used by CI:
- `0`: success
- `1`: generic command/runtime failure
- `2`: usage/argument failure
- `10`: scenario/schema invalid
- `20`: backend connection failure
- `30`: threshold evaluation failure

## Design intent

The router is intentionally explicit rather than clever. Early explicit branching is easier to audit, easier to test, and safer for a hardware-focused team where reproducibility matters more than abstraction density.

## Connection to the project

This is where reproducibility starts. If CLI contracts are ambiguous, every downstream test run and triage report becomes harder to trust.
