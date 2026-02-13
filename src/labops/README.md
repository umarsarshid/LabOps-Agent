# src/labops

`src/labops/` owns the CLI surface for the project.

## Why this folder exists

The CLI is the primary user interface for lab engineers and CI pipelines. Keeping CLI concerns here makes it easy to understand what commands exist, what arguments are valid, and what output/exit contracts scripts can trust.

## What belongs here

- Process entrypoint (`main.cpp`).
- Top-level command routing and argument handling.
- Command-level output and exit code contracts.

## Current command contract

- `labops version`: prints tool version.
- `labops validate <scenario.json>`: validates scenario file preflight checks.
- `labops run <scenario.json> --out <dir>`: emits `<dir>/run.json` metadata
  artifact and reports its path.

## What should not live here

- Camera/stream runtime logic.
- Scenario schema internals.
- Metric computation internals.
- Agent planning heuristics.

Those belong in their dedicated modules and are called from here.

## Connection to the project

A reliable CLI contract is critical because everything else (humans, CI, future orchestration services) invokes `labops` through this interface.
