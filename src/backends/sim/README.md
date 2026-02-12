# src/backends/sim

Deterministic simulation backend used for early development and CI.

## Why this folder exists

Hardware is expensive and not always available in every environment. The sim backend lets us test command flow, event generation, metrics, artifacts, and agent logic with reproducible behavior.

## Expected responsibilities

- Simulated stream lifecycle.
- Controlled injection of faults (drops, jitter spikes, disconnect windows).
- Repeatable timing and seeded randomness.

## Design principle

Given the same scenario and seed, sim outputs should be identical. Determinism is required for trustworthy regression tests.

## Connection to the project

The sim backend is the default execution path that enables fast iteration before and alongside real-camera integration.
