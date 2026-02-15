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
- `real_backend_factory_smoke.cpp`: validates real-backend factory behavior in
  both build states (enabled -> real skeleton, disabled -> sdk_stub fallback)
  and verifies actionable skeleton errors without requiring camera hardware.
- `sdk_context_smoke.cpp`: validates real SDK context RAII behavior (init once,
  handle reference counting, and safe shutdown on final release).
- `real_device_enumeration_smoke.cpp`: validates real-device descriptor
  enumeration/mapping into normalized `DeviceInfo` fields (including transport
  normalization and optional IP/MAC/version handling).
- `real_device_selector_resolution_smoke.cpp`: validates selector parsing and
  deterministic resolution rules (`serial`/`user_id` with optional 0-based
  `index` disambiguation) both against in-memory fixture devices and
  environment-driven discovery fixtures.
- `node_map_adapter_smoke.cpp`: validates `NodeMapAdapter` contracts for
  pre-apply parameter capability checks (`Has`/type/range/enum listing) and
  typed get/set behavior with actionable failures for mismatched/out-of-range
  writes.

## Connection to the project

If backend contracts are inconsistent, reproducible runs and automated triage
break down. These tests protect the foundation for hardware-agnostic workflows.
