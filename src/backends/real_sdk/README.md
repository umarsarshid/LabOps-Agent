# src/backends/real_sdk

Real-backend skeleton and creation factory for SDK-enabled builds.

## Why this folder exists

The project needs a concrete, compile-time real backend path that can be wired
into run orchestration without shipping proprietary vendor code in this repo.
This folder is that bridge.

## Current contents

- `real_backend.hpp` / `real_backend.cpp`:
  - `RealBackend` implements `ICameraBackend`.
  - behavior is intentionally deterministic and non-streaming for now.
  - returns actionable "not implemented yet" errors where SDK calls will land.
- `real_backend_factory.hpp` / `real_backend_factory.cpp`:
  - centralizes build-status helpers for CLI (`enabled`, `SDK missing`, etc.)
  - creates the effective backend object for real runs:
    - real backend enabled -> `RealBackend`
    - real backend disabled -> `sdk_stub::RealCameraBackendStub`

## Connection to the project

`labops run` and backend visibility commands can now route through one factory
instead of scattering real-backend checks across CLI code. This keeps the real
integration path testable and ready for vendor SDK wiring in later commits.
