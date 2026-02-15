# src/backends/sdk_stub

SDK integration contract stubs (no proprietary vendor code).

## Why this folder exists

We need a clear boundary where vendor-specific camera integration will live, without committing proprietary SDK code into this repository.

## Expected responsibilities

- Interface definitions for capture/session control.
- Capability and setting-mapping contracts.
- Placeholder adapters and mocks for compile-time integration.

## Current contents

- `real_camera_backend_stub.hpp`:
  - declares `RealCameraBackendStub` implementing `ICameraBackend`
  - exposes build-status helpers so CLI can report backend availability and
    reason (`enabled`, `disabled (SDK not found)`, etc.)
  - status helpers are consumed by `real_sdk` factory wiring so one source of
    truth drives backend availability messaging across commands
  - exposes status used by `labops list-devices --backend real` for friendly
    `BACKEND_NOT_AVAILABLE` messaging when real backend is inactive
- `real_camera_backend_stub.cpp`:
  - intentionally avoids vendor headers/binaries
  - returns actionable "not implemented" errors instead of fake streaming
  - preserves requested params in `DumpConfig()` for diagnostics

## Build toggle

- `LABOPS_ENABLE_REAL_BACKEND` (default `OFF`)
  - `OFF`: keeps all builds SDK-free across Linux/macOS/Windows
  - `ON`: enables compile-time integration path marker, but behavior is still
    stubbed until a proprietary adapter is added outside this repo

## Integration guide

- Use `docs/integration/real_sdk_backend.md` as the step-by-step playbook for
  replacing this stub with a real vendor SDK adapter while keeping artifact and
  event contracts stable.

## Guardrails

- Do not commit vendor binaries, headers, or confidential code.
- Keep this layer thin: translate SDK specifics into project-wide backend contracts.

## Connection to the project

This is the extension point that allows swapping camera models/vendors while preserving the same CLI, metrics, artifacts, and agent triage flow.
