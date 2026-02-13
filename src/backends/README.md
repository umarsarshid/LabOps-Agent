# src/backends

`src/backends/` contains camera execution backends.

## Why this folder exists

The project needs one consistent workflow that can run on different camera stacks. Backends provide an adapter layer so the agent can keep the same logic while hardware integrations vary.

## Expected responsibilities

- Shared backend interfaces/contracts.
- Backend selection and capability mapping.
- Isolated implementations under backend-specific subfolders.

## Current contents

- `camera_backend.hpp`: `ICameraBackend` contract (`connect/start/stop`,
  `set_param`, `dump_config`, `pull_frames(duration)`).
- `sim/`: deterministic in-repo implementation of the contract.
- `sdk_stub/`: placeholder integration boundary for vendor SDK adapters.

## Current and planned backends

- `sim/`: deterministic simulator for development and CI.
- `sdk_stub/`: integration interface skeleton for proprietary camera SDKs.

## Connection to the project

This folder is what makes "same autopilot brain, different camera" possible.
