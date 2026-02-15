# src/core/schema

This folder defines shared data contracts that must remain stable across the
CLI, execution engine, artifacts, and agent analysis.

## Why this folder exists

Run metadata is a cross-cutting concern. If each module invents its own shape
for run configuration and timing, reproducibility and diagnostics degrade fast.
Keeping canonical schema types here gives every module one source of truth.

## Current contents

- `run_contract.hpp/.cpp`: `RunConfig` and `RunInfo` contracts plus JSON
  serialization helpers.

## Contract intent

- `RunConfig` captures immutable run inputs (scenario/backend/seed/duration).
- `RunInfo` captures run identity plus lifecycle timestamps.
- `RunInfo.real_device` (optional) captures resolved physical-camera identity
  and version evidence for real runs:
  - `model`, `serial`, `transport`
  - optional `user_id`
  - optional `firmware_version` (when source exposes it)
  - `sdk_version` for real-device runs (`unknown` fallback when unavailable)
- JSON serialization is canonical and deterministic for stable snapshots and
  artifact diffs.

## Connection to the project

This module is part of the evidence backbone. Every triage bundle and agent
decision depends on accurate, consistent run metadata.
