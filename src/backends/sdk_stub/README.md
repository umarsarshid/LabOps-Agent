# src/backends/sdk_stub

SDK integration contract stubs (no proprietary vendor code).

## Why this folder exists

We need a clear boundary where vendor-specific camera integration will live, without committing proprietary SDK code into this repository.

## Expected responsibilities

- Interface definitions for capture/session control.
- Capability and setting-mapping contracts.
- Placeholder adapters and mocks for compile-time integration.

## Guardrails

- Do not commit vendor binaries, headers, or confidential code.
- Keep this layer thin: translate SDK specifics into project-wide backend contracts.

## Connection to the project

This is the extension point that allows swapping camera models/vendors while preserving the same CLI, metrics, artifacts, and agent triage flow.
