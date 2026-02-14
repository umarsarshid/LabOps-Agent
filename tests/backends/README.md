# tests/backends

Backend contract and implementation smoke tests.

## Why this folder exists

Backend code is the hardware abstraction boundary for the entire project.
Tests here verify contract conformance so runtime/agent logic can depend on a
stable interface regardless of real hardware availability.

## Current contents

- `sim_backend_interface_smoke.cpp`: validates that sim backend implements
  `ICameraBackend` and supports core control/pull operations.
- `sim_frame_generator_smoke.cpp`: validates deterministic frame generation
  fields and timing behavior (~N/FPS seconds).
- `sim_fault_injection_smoke.cpp`: validates scenario-controlled fault
  injection knobs and same-seed reproducibility for drop/reorder patterns.
- `sdk_stub_backend_smoke.cpp`: validates that the real-backend stub compiles
  without proprietary SDK dependencies and returns actionable non-implemented
  errors.

## Connection to the project

If backend contracts are inconsistent, reproducible runs and automated triage
break down. These tests protect the foundation for hardware-agnostic workflows.
