# src/core

`src/core/` is for shared foundational contracts used across multiple modules.

## Why this folder exists

As the system grows, many modules need common utilities (errors, clocks, structured logging primitives, IDs, schema helpers). Centralizing those avoids duplicated logic and inconsistent behavior.

## Expected contents over time

- Common error/result types.
- Time and monotonic clock helpers.
- Shared serialization helpers and constants.
- Project-wide logging interfaces.
- Canonical schema contracts under `schema/` for cross-module run metadata.

## Current contents

- `schema/`: run-contract structures and JSON serialization helpers.
- `logging/`: shared structured logger with level parsing and run-id context.
- `errors/`: stable process exit-code contract used by CLI and CI smoke tests.

## Guardrails

- Keep `core` dependency-light and policy-light.
- Avoid placing feature-specific behavior here.
- If logic is only used by one module, keep it local to that module.

## Connection to the project

`core` makes run outputs and failure classification consistent across CLI, backends, metrics, and agent analysis, which is necessary for trustworthy engineer packets.
