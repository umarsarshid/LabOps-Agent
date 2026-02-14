# LabOps Agent

C++-first autopilot lab assistant for repeatable camera testing and faster triage.

## What This Project Is

LabOps Agent runs camera scenarios the same way every time, captures evidence
automatically, and prepares the foundation for AI-assisted root-cause
isolation.

Simple flow:
- You run a scenario.
- LabOps executes the backend with deterministic controls.
- It records a per-run bundle with `scenario.json`, `run.json`,
  `events.jsonl`, `metrics.csv`, `metrics.json`, `summary.md`,
  `report.html`, and `hostprobe.json` plus raw NIC command outputs
  (`nic_*.txt`).
- Tests verify contracts and determinism so behavior stays reproducible.

## Current Status

The repo currently has a working CLI, deterministic sim backend, scenario pack,
artifact/event/metrics output pipeline, and CI-validated integration tests.
Bundle packaging is implemented (manifest + optional zip), and triage bundle
contracts are documented as an internal tooling spec in
`docs/triage_bundle_spec.md`. Runs now also emit both a one-page `summary.md`
and a static `report.html` artifact for quick human/browser triage.
Agent-mode execution is still upcoming, but the experiment-state model and
`agent_state.json` serialization contract now exist, and a first in-process
`ExperimentRunner` can execute baseline + one variant automatically. A
symptom-driven playbook selector is now in place for deterministic knob
ordering, plus an OAAT variant generator that writes scenario mutations to
`out/agent_runs/`.

## Milestone Progress

- Milestone 0: done (repo/build/style/CI foundation)
- Milestone 1: done (CLI skeleton + strict output contracts)
- Milestone 2: done (sim backend + deterministic/fault-injected stream runs)
- Milestone 3: done (FPS/drop/jitter metrics + metric artifacts)
- Milestone 4: done (scenario schema, validation, scenario application in run)
- Milestone 5: done (bundle layout, manifest, optional support zip, bundle docs)
- Milestone 6: done (baseline capture + compare diff outputs + threshold pass/fail)
- Milestone 7: done (host probe + redaction evidence capture)
- Milestone 8: done (netem profiles, command suggestions, guarded Linux apply path)
- Milestone 9: in progress (agent mode foundations)

## Implemented So Far

- Project/build foundation with CMake.
- Cross-platform CI (`ubuntu`, `macos`, `windows`) with `ctest`.
- CLI commands:
  - `labops version`
  - `labops validate <scenario.json>`
  - `labops run <scenario.json> [--out <dir>] [--zip] [--redact] [--log-level <debug|info|warn|error>] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`
  - `labops baseline capture <scenario.json> [--redact] [--log-level <debug|info|warn|error>] [--apply-netem --netem-iface <iface> [--apply-netem-force]]`
  - `labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]`
  - stable process exit-code contract for automation:
    - `0` success, `1` generic failure, `2` usage error
    - `10` schema invalid, `20` backend connect failed, `30` thresholds failed
- Scenario loader + schema validation in `labops validate` with actionable
  field-level errors.
- Netem profile definitions under `tools/netem_profiles/` (jitter/loss/reorder
  presets) with scenario reference validation via optional `netem_profile`.
- Starter scenario set in `scenarios/`:
  - `sim_baseline.json`
  - `dropped_frames.json`
  - `trigger_roi.json`
- Run contract schema (`RunConfig`, `RunInfo`) with JSON serialization.
- Artifact writers for per-run bundle files:
  - `<out>/<run_id>/scenario.json`
  - `<out>/<run_id>/hostprobe.json`
  - `<out>/<run_id>/nic_*.txt`
  - `<out>/<run_id>/run.json`
  - `<out>/<run_id>/events.jsonl`
  - `<out>/<run_id>/metrics.csv`
  - `<out>/<run_id>/metrics.json`
  - `<out>/<run_id>/summary.md`
  - `<out>/<run_id>/report.html`
  - `<out>/<run_id>/bundle_manifest.json`
  - optional `<out>/<run_id>.zip` support bundle when `--zip` is requested
- Baseline capture command:
  - `labops baseline capture <scenario.json> [--redact]`
  - writes baseline artifacts directly under `baselines/<scenario_id>/`
  - baseline directory includes `metrics.csv`, `metrics.json`, `summary.md`,
    `report.html`, `hostprobe.json`, and raw NIC command outputs (`nic_*.txt`)
- Compare command:
  - `labops compare --baseline ... --run ...`
  - compares summary metrics and writes `diff.json` + `diff.md`
  - writes compare outputs to run target by default (or `--out <dir>`)
- Threshold enforcement in run flow:
  - evaluates scenario thresholds after metrics computation
  - returns non-zero on threshold violations
- Stream lifecycle event emission in `labops run`:
  - `CONFIG_APPLIED`
  - `STREAM_STARTED`
  - `FRAME_RECEIVED`
  - `FRAME_DROPPED`
  - `STREAM_STOPPED`
- Structured runtime logging:
  - `--log-level <debug|info|warn|error>` for run/baseline capture verbosity
  - log lines include `run_id` once run planning starts, so stderr logs can be
    correlated directly to run bundle artifacts
- FPS metrics pipeline:
  - computes `avg_fps` over run duration
  - computes `rolling_fps` over a fixed window
  - computes drop stats (`total dropped`, `drop rate percent`)
  - computes inter-frame interval stats (`min/avg/p95-ish`)
  - computes inter-frame jitter stats (`min/avg/p95-ish`)
  - derives named anomaly heuristics for summary output:
    `resend spike`, `jitter cliff`, `periodic stall`
  - writes `<out>/<run_id>/metrics.csv` and `<out>/<run_id>/metrics.json`
- One-page run summary pipeline:
  - writes `<out>/<run_id>/summary.md`
  - includes run status (`PASS`/`FAIL`), key metrics, threshold findings, and
    top anomalies
  - adds optional manual Linux netem apply/show/teardown suggestions when
    `netem_profile` is set and a profile definition is available
- Static HTML run report pipeline:
  - writes `<out>/<run_id>/report.html`
  - includes browser-friendly key metrics, expected-vs-actual deltas, and
    rolling FPS table rows (plots-ready, no JS)
- Optional netem execution pipeline (Linux only):
  - executes only when `--apply-netem` is provided with `--netem-iface`
  - requires root unless `--apply-netem-force` is explicitly used
  - always attempts teardown on exit via scope-guard safety
- Agent experiment-state contract:
  - canonical model for hypotheses, tested variables, and results table
  - deterministic JSON serialization into `agent_state.json`
  - dedicated smoke test to keep this artifact contract stable
- Agent experiment runner foundation:
  - `ExperimentRunner` executes baseline capture + one variant run
    automatically in-process
  - reuses the same scenario runner pipeline as CLI (`ExecuteScenarioRun`)
    rather than shelling out to subprocess commands
- Agent playbook framework:
  - maps symptom input to an ordered list of knobs to try
  - current symptom coverage:
    - `dropped_frames` -> `packet_delay_ms`, `fps`, `roi_enabled`,
      `reorder_percent`, `loss_percent`
- Agent OAAT variant generation:
  - takes base scenario + symptom playbook and emits one-knob-diff scenario
    files
  - default output location: `out/agent_runs/`
  - emits `variants_manifest.json` with knob/path/value deltas
- Agent stop-condition framework:
  - deterministic stop reasons with fixed priority:
    - max runs
    - single-variable flip
    - confidence threshold
    - stable repro rate
  - returns explicit human-readable explanation for why execution stopped
- Engineer packet generation:
  - writes `engineer_packet.md` with reproducible handoff content:
    - repro steps
    - configs tried
    - what changed
    - what we ruled out
    - ranked hypotheses + evidence links
  - includes exact artifact and diff paths for evidence traceability
- Host probe pipeline:
  - writes `<out>/<run_id>/hostprobe.json`
  - includes OS/CPU/RAM/uptime/load snapshot and parsed NIC highlights
    (including MTU + link speed hints when available) for hardware/software
    triage
  - writes platform NIC raw command outputs as `nic_*.txt`
  - supports optional identifier redaction (`--redact`) so obvious host/user
    values are replaced before bundle files are written
- Backend contract (`ICameraBackend`) plus deterministic sim backend.
- Sim features:
  - Configurable FPS, jitter, seed, frame size.
  - Fault knobs (`drop_every_n`, `drop_percent`, `burst_drop`, `reorder`).
  - Deterministic output for the same seed/scenario.
- Test coverage:
  - Schema JSON smoke tests.
  - Artifact writer smoke tests.
  - Event JSONL smoke tests.
  - Backend interface, frame generation, and fault injection smoke tests.
  - CLI run trace smoke test.
  - Starter scenarios end-to-end smoke test.
  - Metrics writer smoke test (`metrics.csv` + `metrics.json`).
  - Jitter injection smoke test.
  - Drop injection smoke test.
  - Determinism golden smoke test (same seed => same first K normalized events).
- Baseline scenario integration smoke test validating expected metric ranges.
- Baseline capture smoke test validating `baselines/<scenario_id>/` output contract.
- Compare diff smoke test validating `diff.json` + `diff.md` delta generation.
- Threshold failure smoke test validating non-zero `labops run` exit on violations.
- Hostprobe redaction smoke test validating host/user identifiers are stripped
  from hostprobe JSON + NIC raw text outputs.
- Netem option contract smoke test validating safe flag pairing for
  `--apply-netem` and `--netem-iface`.
- Run summary/report smoke coverage validating `summary.md` and `report.html`
  presence/readability.
- Catch2 core unit tests for schema/event JSON serialization (when available).
- Internal triage bundle spec in `docs/triage_bundle_spec.md` covering:
  - bundle lifecycle and directory contract
  - required/optional artifacts with file-level contracts
  - manifest integrity behavior
  - operational verification checklist and compatibility guidance

## Not Implemented Yet

- Scenario schema expansion (current validator covers core fields and
  constraints; deeper domain-specific rules can be added).
- Full metrics suite completion (disconnect-specific metrics beyond current FPS+drop+jitter timing).
- SDK-backed camera implementation (only interface boundary/stub exists).
- Full agent planning policy (multi-step hypothesis ranking/selection) and
  final engineer packet generation (baseline+variant orchestration exists).

## Formatting

This repo uses `.clang-format` for C/C++ style consistency.

- CI pins `clang-format` major `21` to avoid version-dependent wrapping drift.
- Check formatting:
  - `bash tools/clang_format.sh --check`
- Apply formatting:
  - `bash tools/clang_format.sh --fix`
- If your system has multiple formatter binaries, you can select one explicitly:
  - `CLANG_FORMAT_BIN=clang-format-21 CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check`

If `clang-format` is not installed locally, install it first and rerun the
command.

## Quick Start

### 1) Configure and build

```bash
cmake -S . -B build
cmake --build build
```

### 2) Check CLI

```bash
./build/labops version
```

### 3) Validate a starter scenario

```bash
./build/labops validate scenarios/sim_baseline.json
```

### 4) Run baseline scenario

```bash
./build/labops run scenarios/sim_baseline.json --out out/
```

### 5) Run fault-repro scenario (optional)

```bash
./build/labops run scenarios/dropped_frames.json --out out-drops/
```

### 6) Create support zip on demand (optional)

```bash
./build/labops run scenarios/sim_baseline.json --out out/ --zip
```

### 7) Run with redaction enabled (optional)

```bash
./build/labops run scenarios/sim_baseline.json --out out-redacted/ --redact
```

### 8) Capture baseline metrics for a scenario (optional)

```bash
./build/labops baseline capture scenarios/sim_baseline.json
```

### 9) Compare baseline vs run (optional)

```bash
./build/labops compare --baseline baselines/sim_baseline --run out/<run_id>
```

### 10) Apply netem during run (Linux, explicit, optional)

```bash
sudo ./build/labops run scenarios/trigger_roi.json --out out-netem/ --apply-netem --netem-iface eth0
```

## Demo Commands (Internal Tooling Flow)

These are practical command patterns operators use during triage.

### 1) Capture a known-good baseline

```bash
./build/labops baseline capture scenarios/sim_baseline.json
```

Expected output shape:

```text
baseline captured: scenarios/sim_baseline.json
run_id: run-<timestamp_ms>
bundle: baselines/sim_baseline
metrics_csv: baselines/sim_baseline/metrics.csv
summary: baselines/sim_baseline/summary.md
report_html: baselines/sim_baseline/report.html
thresholds: pass
```

### 2) Run a fault-injection scenario and produce a bundle

```bash
./build/labops run scenarios/dropped_frames.json --out /tmp/labops-demo
```

Expected output shape:

```text
run queued: scenarios/dropped_frames.json
run_id: run-<timestamp_ms>
bundle: /tmp/labops-demo/run-<timestamp_ms>
events: /tmp/labops-demo/run-<timestamp_ms>/events.jsonl
metrics_json: /tmp/labops-demo/run-<timestamp_ms>/metrics.json
summary: /tmp/labops-demo/run-<timestamp_ms>/summary.md
report_html: /tmp/labops-demo/run-<timestamp_ms>/report.html
thresholds: pass
```

### 3) Compare run metrics against the baseline

```bash
RUN_DIR="$(find /tmp/labops-demo -maxdepth 1 -type d -name 'run-*' | head -n 1)"
./build/labops compare --baseline baselines/sim_baseline --run "$RUN_DIR"
```

Expected output shape:

```text
compare baseline: baselines/sim_baseline/metrics.csv
compare run: /tmp/labops-demo/run-<timestamp_ms>/metrics.csv
diff_json: /tmp/labops-demo/run-<timestamp_ms>/diff.json
diff_md: /tmp/labops-demo/run-<timestamp_ms>/diff.md
compared_metrics: 9
```

Expected bundle layout per run:
- `<out-dir>/<run_id>/scenario.json`
- `<out-dir>/<run_id>/hostprobe.json`
- `<out-dir>/<run_id>/nic_*.txt`
- `<out-dir>/<run_id>/run.json`
- `<out-dir>/<run_id>/events.jsonl`
- `<out-dir>/<run_id>/metrics.csv`
- `<out-dir>/<run_id>/metrics.json`
- `<out-dir>/<run_id>/summary.md`
- `<out-dir>/<run_id>/report.html`
- `<out-dir>/<run_id>/bundle_manifest.json`
- optional `<out-dir>/<run_id>.zip` when `--zip` is used

If you want to author new scenarios, follow `docs/scenario_schema.md`.

## Output Contracts

### Bundle Directory

- `labops run` now writes a dedicated bundle directory per run:
  - `<out>/<run_id>/`
- This keeps repeated runs under the same `--out` root isolated and easy to share.

### Support Zip

- `labops run --zip` writes an additional support archive:
  - `<out>/<run_id>.zip`
- Zip generation is opt-in so default runs avoid extra packaging overhead.

### Identifier Redaction

- `labops run ... --redact` applies best-effort identifier redaction before
  writing `hostprobe.json` and `nic_*.txt`.
- `labops baseline capture ... --redact` uses the same redaction behavior.
- Current replacements target obvious hostname/username tokens and replace them
  with `<redacted_host>` / `<redacted_user>`.

### Netem Execution

- `labops run ... --apply-netem --netem-iface <iface>` attempts to apply netem
  impairment on Linux before stream start.
- By default this requires root; use `--apply-netem-force` only when you
  explicitly want a non-root attempt.
- `labops` always attempts teardown on exit after successful apply to avoid
  leaving host network impairment behind.

### Baseline Capture

- `labops baseline capture <scenario.json> [--redact]` writes artifacts to:
  - `baselines/<scenario_id>/`
- This folder is the stable baseline target for future regression comparison.
- Baseline capture currently emits the same core evidence set as run mode,
  including `metrics.csv`, `metrics.json`, `summary.md`, `report.html`,
  `hostprobe.json`, and `nic_*.txt`.

### Compare Diff Output

- `labops compare --baseline ... --run ...` reads both `metrics.csv` files and
  computes per-metric deltas.
- Output artifacts:
  - `diff.json` (machine-readable deltas)
  - `diff.md` (human-readable delta table)
- Compare outputs default to the run target unless `--out <dir>` is provided.

### Threshold Pass/Fail

- `labops run` checks scenario threshold fields against computed metrics.
- Violations currently include:
  - `avg_fps < min_avg_fps`
  - `drop_rate_percent > max_drop_rate_percent`
  - `inter_frame_interval_p95_us > max_inter_frame_interval_p95_us`
  - `inter_frame_jitter_p95_us > max_inter_frame_jitter_p95_us`
- On violation:
  - process exits non-zero
  - bundle artifacts are still written for investigation

### scenario.json

- Byte-for-byte snapshot copy of the source scenario used for the run.
- Stored at `<out>/<run_id>/scenario.json`.

### hostprobe.json

- Host context snapshot written by `labops run`.
- Includes OS, CPU, RAM total bytes, uptime, load snapshot, and parsed NIC
  highlights (including MTU and link speed hints when available).
- Stored at `<out>/<run_id>/hostprobe.json`.

### nic_*.txt

- Raw NIC command outputs captured best-effort per platform.
- Examples:
  - Windows: `nic_ipconfig_all.txt`
  - Linux: `nic_ip_a.txt`, `nic_ip_r.txt`, `nic_ethtool.txt` (if available)
  - macOS: `nic_ifconfig_a.txt`, `nic_netstat_rn.txt`, `nic_route_get_default.txt`
- Stored at `<out>/<run_id>/nic_*.txt`.

### bundle_manifest.json

- Lists bundle artifacts with relative file paths, `size_bytes`, and hash values.
- Current hash algorithm: `fnv1a_64`.
- Stored at `<out>/<run_id>/bundle_manifest.json`.

### summary.md

- One-page human-readable run report written by `labops run`.
- Includes:
  - run status (`PASS`/`FAIL`)
  - key identity fields (`run_id`, `scenario_id`, backend, seed, timestamps)
  - key metrics table (FPS/drop/timing highlights)
  - threshold findings and top anomalies
  - optional `Netem Commands (Manual)` section when scenario uses
    `netem_profile`
- Stored at `<out>/<run_id>/summary.md`.

### report.html

- Static browser-friendly run report written by `labops run`.
- Includes:
  - run identity block
  - key metrics table
  - expected-vs-actual delta table for quick drift review
  - rolling FPS sample table for chart handoff
  - threshold and anomaly sections
- Designed to be fully static (no JavaScript/runtime dependencies).
- Stored at `<out>/<run_id>/report.html`.

### run.json

Current fields:
- `run_id`
- `config.scenario_id`
- `config.backend`
- `config.seed`
- `config.duration_ms`
- `timestamps.created_at_utc`
- `timestamps.started_at_utc`
- `timestamps.finished_at_utc`

### events.jsonl

- Append-only timeline log.
- One JSON object per line.
- Current `labops run` emits stream/frame lifecycle events:
  - `CONFIG_APPLIED`
  - `STREAM_STARTED`
  - `FRAME_RECEIVED`
  - `FRAME_DROPPED`
  - `STREAM_STOPPED`

### metrics.csv

- CSV metrics artifact written by `labops run`.
- Includes:
  - one `avg_fps` summary row
  - one `rolling_fps` row per rolling sample window
  - drop summary rows (`drops_total`, `drop_rate_percent`)
  - inter-frame interval `min/avg/p95` rows (microseconds)
  - inter-frame jitter `min/avg/p95` rows (microseconds)

### metrics.json

- JSON metrics artifact written by `labops run`.
- Includes machine-friendly structured fields for:
  - FPS summaries
  - drop summaries
  - inter-frame timing/jitter summaries
  - rolling FPS samples

For exact metric definitions (formulas, units, CSV/JSON contracts, and edge
cases), see `docs/triage_bundle_spec.md`.
For a full internal bundle contract (lifecycle, required files, manifest rules,
verification checklist), use `docs/triage_bundle_spec.md` as the authoritative
reference.

## Testing

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Run key integration smoke tests directly

```bash
ctest --test-dir build -R starter_scenarios_e2e_smoke --output-on-failure
ctest --test-dir build -R sim_baseline_metrics_integration_smoke --output-on-failure
```

### Run determinism test directly

```bash
./build/sim_determinism_golden_smoke
```

### Catch2 unit tests (optional)

Catch2 tests are under `tests/core/`. If Catch2 is not installed locally, enable
fetching:

```bash
cmake -S . -B build -DLABOPS_FETCH_CATCH2=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## CI

GitHub Actions:
- Builds on `ubuntu-latest`, `macos-latest`, and `windows-latest`.
- Runs `ctest` on PRs and pushes.

## Repository Guide

- `src/README.md`
- `docs/README.md`
- `docs/triage_bundle_spec.md`
- `docs/release_verification.md`
- `tests/README.md`
- `scenarios/README.md`

Most `src/` and `tests/` subfolders also include focused `README.md` files.

## Near-Term Roadmap

- Start agent experiment loop (change one variable at a time).
- Generate engineer packet output (repro steps, evidence, likely cause, next steps).
- Add hardware SDK backend implementation behind `ICameraBackend`.
