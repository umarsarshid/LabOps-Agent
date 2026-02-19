# src/labops/soak

`src/labops/soak/` holds soak-mode persistence logic used by `labops run --soak`.

## Why this folder exists

Soak runs can be long and may be paused/resumed safely. Checkpoint and frame-cache behavior is operationally important and should not be buried in CLI routing code.

Separating this into its own module makes the behavior easier to reason about, easier to test directly, and less risky to evolve.

## What lives here

- `checkpoint_store.hpp/.cpp`
: defines checkpoint state and read/write helpers for:
  - `soak_checkpoint.json`
  - `checkpoints/checkpoint_<n>.json`
  - `soak_frames.jsonl`
  - checkpoint JSON loading via shared core DOM parser for stronger malformed
    diagnostics during resume
  - atomic checkpoint file publish semantics so interrupted writes do not
    corrupt existing checkpoint artifacts

## Connection to the project

The CLI (`src/labops/cli/router.cpp`) orchestrates run flow, while this module owns durable soak state persistence. That keeps run control and persistence concerns cleanly separated.
