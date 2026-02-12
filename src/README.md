# src

`src/` contains all production C++ code for the `labops` executable.

## Why this folder exists

This project is intentionally split by responsibilities so we can evolve each part (CLI, metrics, agent, artifacts, backends) without creating a monolith. The separation also maps directly to the lab workflow: define scenario -> run capture -> detect issues -> collect evidence -> isolate cause -> ship engineer packet.

## Module map and project flow

- `labops/`: command-line interface and process entrypoints.
- `scenarios/`: scenario schema, loading, and validation logic.
- `backends/`: camera runtime adapters (sim backend first, SDK integrations later).
- `events/`: normalized runtime event model written as append-only logs.
- `metrics/`: derived performance metrics (FPS, jitter, drops, disconnect windows).
- `artifacts/`: run bundle outputs (`run.json`, `metrics.csv`, diffs, packet structure).
- `hostprobe/`: host-machine context capture (OS, CPU, NIC, clocks, driver notes).
- `agent/`: experiment loop that changes one variable at a time to isolate failures.
- `core/`: shared primitives and contracts used by multiple modules.

## Layering rule

Higher-level modules may depend on lower-level contracts, but low-level modules should not depend on high-level policy.

Practical example: `agent/` can depend on `scenarios/`, `metrics/`, and `artifacts/`; `metrics/` should not depend on `agent/`.

## Connection to project outcome

Everything in `src/` exists to produce reproducible runs and explainable triage. If a new file does not help run tests consistently, collect reliable evidence, or isolate a cause faster, it likely belongs elsewhere.
