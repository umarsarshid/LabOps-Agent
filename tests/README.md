# tests

Test suites for unit, integration, and regression coverage.

## Why this folder exists

LabOps must be predictable. Tests verify command contracts, scenario validation, backend behavior, metric correctness, and artifact stability.

## Expected test layers

- Unit tests: fast checks for local module behavior.
- Integration tests: multi-module flows (scenario -> run -> events -> metrics -> artifacts).
- Golden/regression tests: stable output checks for JSON/CSV/report formats.

## Current framework split

- Catch2 unit tests under `tests/core/` (schema/event JSON contracts).
- Lightweight smoke executables for artifact/event path validation in other
  subfolders, including backend interface conformance checks.
- Artifact-specific writer contract tests under `tests/artifacts/`.
- CLI integration smoke tests under `tests/labops/` for end-to-end run traces.
- CLI netem option contract smoke coverage for safe flag pairing.
- Bundle layout consistency smoke coverage for `<out>/<run_id>/...` contracts.
- Optional support-zip smoke coverage for `--zip` bundle packaging.
- Seeded determinism smoke coverage to prevent non-repeatable sim behavior.
- Metric smoke tests under `tests/metrics/` for FPS/drop/jitter formulas and
  CSV/JSON output contracts.
- Scenario validation smoke tests under `tests/scenarios/` for actionable
  schema errors plus baseline-scenario metric-range integration checks.
- Host probe smoke tests under `tests/hostprobe/` for identifier redaction
  guarantees in bundle evidence.
- Agent-state smoke tests under `tests/agent/` for
  `agent_state.json` serialization/writer contract stability.

## Design principle

Tests should favor determinism and clear failure messages so regressions are easy to triage.

## Connection to the project

Without test coverage, confidence in automated triage conclusions drops quickly. This folder safeguards reliability as the system scales.
