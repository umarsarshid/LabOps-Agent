# tests/backends

Backend contract and implementation smoke tests.

## Why this folder exists

Backend code is the hardware abstraction boundary for the entire project.
Tests here verify contract conformance so runtime/agent logic can depend on a
stable interface regardless of real hardware availability.

## Current contents

- `sim_backend_interface_smoke.cpp`: validates that sim backend implements
  `ICameraBackend` and supports core control/pull operations.

## Connection to the project

If backend contracts are inconsistent, reproducible runs and automated triage
break down. These tests protect the foundation for hardware-agnostic workflows.
