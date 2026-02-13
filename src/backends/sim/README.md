# src/backends/sim

Deterministic simulation backend used for early development and CI.

## Why this folder exists

Hardware is expensive and not always available in every environment. The sim backend lets us test command flow, event generation, metrics, artifacts, and agent logic with reproducible behavior.

## Expected responsibilities

- Simulated stream lifecycle.
- Controlled injection of faults (drops, jitter spikes, disconnect windows).
- Repeatable timing and seeded randomness.

## Current contents

- `scenario_config.hpp/.cpp`: scenario-driven config object and adapter used to
  apply sim knobs (`fps`, `jitter_us`, `seed`, and fault settings) via backend
  params. The adapter can also return the exact params applied so run
  orchestration can log a `CONFIG_APPLIED` event for traceability.
- `sim_camera_backend.hpp/.cpp`: `SimCameraBackend` implementation of
  `ICameraBackend` with strict connect/start/stop state transitions,
  parameter mutation, config dump, and deterministic frame pulls based on
  `fps`, `jitter_us`, and `seed`.
- Generated frames include `frame_id`, timestamp, `size_bytes`, and optional
  dropped markers when `drop_every_n` is configured.
- Fault injection knobs include:
  - `drop_percent`
  - `burst_drop`
  - `reorder`

## Design principle

Given the same scenario and seed, sim outputs should be identical. Determinism is required for trustworthy regression tests.

## Connection to the project

The sim backend is the default execution path that enables fast iteration before and alongside real-camera integration.
