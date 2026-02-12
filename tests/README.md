# tests

Test suites for unit, integration, and regression coverage.

## Why this folder exists

LabOps must be predictable. Tests verify command contracts, scenario validation, backend behavior, metric correctness, and artifact stability.

## Expected test layers

- Unit tests: fast checks for local module behavior.
- Integration tests: multi-module flows (scenario -> run -> events -> metrics -> artifacts).
- Golden/regression tests: stable output checks for JSON/CSV/report formats.

## Design principle

Tests should favor determinism and clear failure messages so regressions are easy to triage.

## Connection to the project

Without test coverage, confidence in automated triage conclusions drops quickly. This folder safeguards reliability as the system scales.
