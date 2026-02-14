# docs/integration

Integration playbooks for connecting LabOps to external systems and hardware SDKs.

## Why this folder exists

`docs/` already explains runtime behavior and artifact contracts. This folder is
for implementation guides that tell engineers exactly how to add integrations
without changing core LabOps behavior.

## Expected contents

- Backend integration guides (SDK adapters, capability mapping).
- Step-by-step migration notes for replacing stubs with real adapters.
- Verification checklists for cross-platform build/test readiness.

## Current docs

- `real_sdk_backend.md`: how to add a real camera SDK backend using the
  existing `ICameraBackend` contract.

## Connection to the project

This folder is the bridge between product-level behavior (repeatable run
artifacts) and vendor-specific implementation details.
