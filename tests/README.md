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
- CLI integration smoke tests under `tests/labops/` for end-to-end run traces.
- Seeded determinism smoke coverage to prevent non-repeatable sim behavior.
- Metric smoke tests under `tests/metrics/` for FPS/drop/jitter formulas and CSV output.

## Design principle

Tests should favor determinism and clear failure messages so regressions are easy to triage.

## Connection to the project

Without test coverage, confidence in automated triage conclusions drops quickly. This folder safeguards reliability as the system scales.
