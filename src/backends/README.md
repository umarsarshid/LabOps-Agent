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
  - `FrameSample` now includes `frame_id`, `timestamp`, `size_bytes`, and
    optional `dropped`.
- `sim/`: deterministic in-repo implementation of the contract with
  scenario-controlled fault knobs.
- `sdk_stub/`: non-proprietary real-backend integration boundary.
  - includes `RealCameraBackendStub` so builds can compile without vendor SDKs.
  - controlled by `LABOPS_ENABLE_REAL_BACKEND` build flag (default `OFF`).

## Current and planned backends

- `sim/`: deterministic simulator for development and CI.
- `sdk_stub/`: integration-ready stub path for proprietary camera SDKs.

## Connection to the project

This folder is what makes "same autopilot brain, different camera" possible.
