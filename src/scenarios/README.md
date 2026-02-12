# src/scenarios

`src/scenarios/` handles scenario definitions and validation.

## Why this folder exists

A repeatable lab depends on declarative scenario files. This module ensures scenarios are validated and interpreted the same way every time.

## Expected responsibilities

- Scenario schema and defaults.
- File loading and validation errors.
- Normalized scenario objects passed to executors.

## Design principle

Fail early with clear validation messages. Invalid scenarios should never reach runtime execution.

## Connection to the project

Scenario correctness is the foundation for reproducibility. If scenario parsing is inconsistent, all downstream metrics and triage conclusions become unreliable.
