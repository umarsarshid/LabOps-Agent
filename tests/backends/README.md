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
  and verifies enabled-lifecycle start/stop safety without requiring camera
  hardware.
- `sdk_context_smoke.cpp`: validates real SDK context RAII behavior (init once,
  handle reference counting, and safe shutdown on final release).
- `real_stream_session_smoke.cpp`: validates repeated real-backend
  connect/start/stop cycles, idempotent stop semantics, and SDK-context cleanup
  between runs to prevent lifecycle leaks.
- `real_frame_acquisition_smoke.cpp`: validates single-thread real-frame pull
  loop behavior (timestamp progression plus received/timeout/incomplete
  outcomes), confirms metrics are computable from real-backend samples, and
  verifies frame-rate control changes measured FPS approximately when supported.
- `real_acquisition_loop_mock_provider_smoke.cpp`: validates the real
  acquisition loop through a scripted `MockFrameProvider` (no SDK/hardware)
  including timeout and incomplete outcomes, burst-stall timestamp gaps,
  and event/counter summaries.
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
- `mock_node_map_adapter_smoke.cpp`: validates `ApplyParams` behavior against
  a dedicated mock adapter (no hardware) for:
  enum mapping, numeric range validation/clamping, strict vs best-effort
  unsupported handling, and ROI apply ordering (`width/height` before offsets).
- `param_key_map_smoke.cpp`: validates loading of the data-driven generic-key
  to SDK-node mapping (`maps/param_key_map.json`) and verifies behavior changes
  through JSON edits alone, so mapping updates do not require core code edits.
- `real_apply_params_smoke.cpp`: validates strict/best-effort real-parameter
  application behavior, including unsupported handling and adjusted value
  reporting plus per-setting readback rows (`requested/actual/supported`) used
  by `config_verify.json`, `camera_config.json`, and `config_report.md`,
  including numeric range guards for `exposure`/`gain` and enum validation
  for `pixel_format`, trigger enum round-trips
  (`trigger_mode`, `trigger_source`, `trigger_activation`), plus deterministic ROI ordering
  (`roi_width`, `roi_height`, `roi_offset_x`, `roi_offset_y`) with constraint
  clamping evidence, plus best-effort-only behavior for unsupported
  `frame_rate` and GigE transport tuning keys
  (`packet_size_bytes`, `inter_packet_delay_us`).
- `real_transport_counters_smoke.cpp`: validates best-effort transport counter
  collection alias handling for real runs (`resends`, `packet_errors`,
  `dropped_packets`) so missing/invalid SDK values become explicit
  not-available evidence rather than run failures.
- `real_error_mapper_smoke.cpp`: validates real-backend error classification
  into stable codes plus actionable text so routing/UX can remain consistent
  even if SDK/vendor wording changes.

## Connection to the project

If backend contracts are inconsistent, reproducible runs and automated triage
break down. These tests protect the foundation for hardware-agnostic workflows.
