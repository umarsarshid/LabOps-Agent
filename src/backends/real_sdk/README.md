# src/backends/real_sdk

Real-backend skeleton and creation factory for SDK-enabled builds.

## Why this folder exists

The project needs a concrete, compile-time real backend path that can be wired
into run orchestration without shipping proprietary vendor code in this repo.
This folder is that bridge.

## Current contents

- `node_map_adapter.hpp` / `node_map_adapter.cpp`:
  - introduces `INodeMapAdapter` abstraction for generic parameter keys.
  - supports pre-apply capability checks (`Has`, type query, enum/range query)
    and typed get/set contracts used by future SDK node mapping.
  - keeps early bridge logic deterministic with an in-memory implementation so
    behavior is testable before proprietary node APIs are linked.
- `param_key_map.hpp` / `param_key_map.cpp`:
  - loads data-driven generic key -> SDK node-name mappings from JSON.
  - exposes lookup helpers so integration can evolve node naming without
    touching core run orchestration code.
  - supports file-level map overrides for local/vendor-specific updates.
- `apply_params.hpp` / `apply_params.cpp`:
  - bridges scenario generic keys to SDK node names using `ParamKeyMap`.
  - validates/coerces values via `NodeMapAdapter` in strict or best-effort mode.
  - validates enumeration knobs (for example `pixel_format`) against enum
    entries exposed by the node map adapter and reports allowed values when
    unsupported.
  - supports trigger enum knobs:
    - `trigger_mode` (`free_run`, `software`, `hardware`)
    - `trigger_source` (`line0`, `line1`, `software`)
    - `trigger_activation` (`rising_edge`, `falling_edge`, `any_edge`)
    with readback evidence per key.
  - treats `frame_rate` as best-effort-only:
    - if supported, applies/clamps and surfaces measurable FPS changes
    - if unsupported/read-only, records unsupported evidence and continues
      even in strict apply mode.
  - treats GigE transport tuning as best-effort-only:
    - `packet_size_bytes` (`GevSCPSPacketSize`) with range `[576, 9000]`
    - `inter_packet_delay_us` (`GevSCPD`) with range `[0, 100000]`
    - applies only when resolved transport is GigE; otherwise records
      transport-aware unsupported evidence and continues.
  - includes first practical numeric knob guards for real triage tickets:
    - `exposure` (`ExposureTime`) clamped/validated to `[5, 10000000]` (us)
    - `gain` (`Gain`) clamped/validated to `[0, 48]` (dB)
    - ROI nodes:
      - `roi_width` (`Width`) clamped/validated to `[64, 4096]` (px)
      - `roi_height` (`Height`) clamped/validated to `[64, 2160]` (px)
      - `roi_offset_x` (`OffsetX`) clamped/validated to `[0, 4095]` (px)
      - `roi_offset_y` (`OffsetY`) clamped/validated to `[0, 2159]` (px)
    - applies ROI with deterministic size-before-offset ordering:
      `roi_width`, `roi_height`, `roi_offset_x`, `roi_offset_y`.
  - performs per-setting readback verification and returns structured
    `requested vs actual vs supported` rows.
  - returns structured applied/unsupported/adjusted results so CLI can emit
    deterministic config events plus `config_verify.json`,
    `camera_config.json`, and `config_report.md` evidence
    (`CONFIG_APPLIED`, `CONFIG_UNSUPPORTED`, `CONFIG_ADJUSTED`).
- `maps/param_key_map.json`:
  - default mapping data for first integration keys:
    - `exposure`, `gain`, `pixel_format`,
      `packet_size_bytes`, `inter_packet_delay_us`,
      `roi_width`, `roi_height`, `roi_offset_x`, `roi_offset_y`,
      `roi` (legacy alias),
      `trigger_mode`, `trigger_source`, `trigger_activation`, `frame_rate`
- `real_backend.hpp` / `real_backend.cpp`:
  - `RealBackend` implements `ICameraBackend`.
  - `connect/start/stop` lifecycle is now wired through SDK/context-safe RAII.
  - stop behavior is idempotent so repeated cleanup paths stay safe.
  - includes a deterministic single-thread frame acquisition loop that emits
    per-frame outcomes (`received`, `timeout`, `incomplete`) in OSS builds.
  - uses seeded outcome generation so repeated runs produce stable evidence
    while vendor pull APIs are still being integrated.
  - uses `SdkContext` RAII to guard process-level SDK init/shutdown safely.
  - keeps lifecycle and timestamp scaffolding aligned with future vendor pull
    adapter wiring.
- `stream_session.hpp` / `stream_session.cpp`:
  - encapsulates acquisition start/stop semantics for real backend runs.
  - guarantees best-effort stop in destructor and idempotent explicit stop calls.
  - isolates session lifecycle bookkeeping so backend orchestration can safely
    stop in both success and error paths.
- `sdk_context.hpp` / `sdk_context.cpp`:
  - process-level SDK lifecycle wrapper.
  - guarantees init happens once across concurrent backend handles.
  - guarantees shutdown happens when the final active handle releases.
- `real_backend_factory.hpp` / `real_backend_factory.cpp`:
  - centralizes build-status helpers for CLI (`enabled`, `SDK missing`, etc.)
  - defines normalized `DeviceInfo` used by `labops list-devices`
  - maps SDK-style device descriptors into `DeviceInfo` fields
    (`model/serial/user_id/transport/ip/mac`) plus optional version fields
    (`firmware_version`, `sdk_version`) when discovery sources expose them
  - supports local OSS discovery fixture via `LABOPS_REAL_DEVICE_FIXTURE` CSV
    so discovery behavior is testable without vendor SDK binaries
    (optional extra columns: `firmware_version`, `sdk_version`)
  - parses and resolves deterministic selectors used by CLI/scenarios:
    - `serial:<value>`
    - `user_id:<value>`
    - optional `index:<n>` (0-based) to disambiguate multi-match cases
  - exposes one-shot `ResolveConnectedDevice(...)` helper used by run/baseline
    command paths before backend connect so selected camera identity is
    captured in run evidence (`run.json` real-device metadata)
  - creates the effective backend object for real runs:
    - real backend enabled -> `RealBackend`
    - real backend disabled -> `sdk_stub::RealCameraBackendStub`

## Connection to the project

`labops run` and backend visibility commands can now route through one factory
instead of scattering real-backend checks across CLI code. This keeps the real
integration path testable and ready for vendor SDK wiring in later commits.
