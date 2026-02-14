# LabOps Agent

LabOps is a command-line tool that helps camera engineers run tests the same way every time and collect clean evidence.

## In Plain English

You give LabOps a test file (called a scenario).
LabOps runs that test, records what happened, and saves a folder of evidence.
Then you can compare that run to a known-good run (baseline) to see what changed.

## What You Give It

- A scenario JSON file (example: `scenarios/sim_baseline.json`)
- A place to save output (example: `tmp/runs`)

## What It Gives You

For each run, you get a folder with files like:

- `run.json`: run settings and metadata
- `events.jsonl`: timeline of stream events
- `metrics.csv` and `metrics.json`: FPS, drops, timing numbers
- `summary.md`: quick human-readable summary
- `report.html`: browser-friendly report
- `bundle_manifest.json`: list of files + hashes
- `hostprobe.json` and `nic_*.txt`: host/network context

Optional extras:

- `diff.json` + `diff.md` when you compare against a baseline
- `kb_draft.md` when you convert an engineer packet into a KB draft
- zip bundle (`--zip`)
- soak checkpoint files (`--soak` mode)

## Quick Start

```bash
cmake -S . -B tmp/build
cmake --build tmp/build
```

```bash
./tmp/build/labops version
./tmp/build/labops list-backends
./tmp/build/labops validate scenarios/sim_baseline.json
./tmp/build/labops run scenarios/sim_baseline.json --out tmp/runs
```

## Typical Workflow

1. Validate a scenario.
2. Capture a baseline once.
3. Run your test scenario.
4. Compare run vs baseline.
5. Review summary/report/evidence files.
6. (Optional) Generate KB draft from engineer packet.

## Example Commands (End-to-End)

```bash
LABOPS=./tmp/build/labops
SCENARIO=scenarios/dropped_frames.json
BASELINE_SCENARIO=scenarios/sim_baseline.json
OUT_ROOT=tmp/runs_camera_team
mkdir -p "$OUT_ROOT"

# 1) Validate
"$LABOPS" validate "$SCENARIO"

# 2) Capture known-good baseline
"$LABOPS" baseline capture "$BASELINE_SCENARIO"

# 3) Run test
"$LABOPS" run "$SCENARIO" --out "$OUT_ROOT"

# 4) Find latest run folder
RUN_DIR="$(find "$OUT_ROOT" -maxdepth 1 -type d -name 'run-*' | sort | tail -n 1)"

# 5) Compare with baseline
"$LABOPS" compare --baseline baselines/sim_baseline --run "$RUN_DIR"

# 6) Optional: KB draft from engineer packet
"$LABOPS" kb draft --run "$RUN_DIR"
```

## Current Scope (Important)

Right now, the default backend is a simulator.
That means LabOps is already useful for repeatable testing and triage flow, even before full real-camera SDK integration.

### What this means in practice

- If you want to test a physical camera today: the real camera backend still needs to be implemented.
- If you want to build and prove your team workflow today: this is already useful (same commands, same evidence files, same compare flow).
- Once the real backend is plugged in, this exact pipeline works on live camera data instead of simulated data.

## Main Commands

- `labops version`
- `labops list-backends`
- `labops validate <scenario.json>`
- `labops run <scenario.json> --out <dir> [--zip] [--redact] [--soak ...]`
- `labops baseline capture <scenario.json>`
- `labops compare --baseline <dir|metrics.csv> --run <dir|metrics.csv> [--out <dir>]`
- `labops kb draft --run <run_folder> [--out <kb_draft.md>]`

## Where To Read More

- `ProjectDesc.md` for deeper architecture details
- `docs/triage_bundle_spec.md` for bundle format details
- `docs/scenario_schema.md` for scenario fields
- `docs/release_verification.md` for release checklist
- `docs/real_backend_setup.md` for local real-SDK setup and safety guardrails

## Dev Commands

```bash
CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check
ctest --test-dir tmp/build --output-on-failure
```
