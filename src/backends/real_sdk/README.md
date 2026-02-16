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
  - performs per-setting readback verification and returns structured
    `requested vs actual vs supported` rows.
  - returns structured applied/unsupported/adjusted results so CLI can emit
    deterministic config events plus `config_verify.json` and
    `camera_config.json` evidence (`CONFIG_APPLIED`, `CONFIG_UNSUPPORTED`,
    `CONFIG_ADJUSTED`).
- `maps/param_key_map.json`:
  - default mapping data for first integration keys:
    - `exposure`, `gain`, `pixel_format`, `roi`,
      `trigger_mode`, `trigger_source`, `frame_rate`
- `real_backend.hpp` / `real_backend.cpp`:
  - `RealBackend` implements `ICameraBackend`.
  - behavior is intentionally deterministic and non-streaming for now.
  - uses `SdkContext` RAII to guard process-level SDK init/shutdown safely.
  - returns actionable "not implemented yet" errors where SDK calls will land.
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
