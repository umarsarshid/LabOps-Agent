# How To Add A Real SDK Backend

This guide explains how to replace the current SDK stub with a real camera SDK
adapter while keeping LabOps behavior and artifacts stable.

## Why this guide exists

LabOps is intentionally split into:
- a stable backend contract (`ICameraBackend`)
- backend-specific implementations (`sim`, future real SDK adapters)

That split lets us add vendor integration without changing triage output
contracts (`events.jsonl`, `metrics.*`, `run.json`, `summary.md`).

## Current integration boundary

Relevant files:
- `src/backends/camera_backend.hpp`
- `src/backends/sdk_stub/real_camera_backend_stub.hpp`
- `src/backends/sdk_stub/real_camera_backend_stub.cpp`
- `src/backends/sim/scenario_config.hpp`
- `src/backends/sim/scenario_config.cpp`
- `src/labops/cli/router.cpp`

Build flag:
- `LABOPS_ENABLE_REAL_BACKEND` (default `OFF`)

Current status:
- the real backend path compiles but intentionally returns
  "not implemented" errors.

## Required backend methods (must implement)

You must implement every `ICameraBackend` method from
`src/backends/camera_backend.hpp`.

### `bool Connect(std::string& error)`

Required behavior:
- open SDK session/resources
- validate target device selection
- return `true` only when backend is ready for `Start`

Failure behavior:
- return `false`
- set actionable error text (include SDK error code if available)

### `bool Start(std::string& error)`

Required behavior:
- transition to streaming state
- configure SDK pipeline state required before frame acquisition

Failure behavior:
- fail if called before successful `Connect`
- return actionable error string

### `bool Stop(std::string& error)`

Required behavior:
- stop stream and release per-stream state
- be safe to call in failure cleanup paths

Failure behavior:
- return actionable error if stop fails

### `bool SetParam(const std::string& key, const std::string& value, std::string& error)`

Required behavior:
- apply one parameter at a time
- validate/convert value units before calling SDK APIs
- persist applied value for `DumpConfig()` and CONFIG_APPLIED evidence

Failure behavior:
- reject unknown/unsupported keys with explicit error
- reject invalid values with explicit min/max/enum expectation

### `BackendConfig DumpConfig() const`

Required behavior:
- return current backend parameter snapshot as string key/value pairs
- include connection/stream state markers (`connected`, `running`)
- include backend identity (for example `backend=vendor_xxx`)

Why this matters:
- this snapshot is used for evidence and debugging
- it should reflect actual applied settings, not requested settings

### `std::vector<FrameSample> PullFrames(std::chrono::milliseconds duration, std::string& error)`

Required behavior:
- acquire frames for requested wall-clock window
- emit stable `FrameSample` data:
  - `frame_id`
  - `timestamp`
  - `size_bytes`
  - optional `dropped`

Failure behavior:
- if acquisition fails, return `{}` and actionable error

## Mapping strategy (scenario -> backend -> SDK)

Use a 3-layer mapping so the core runner stays backend-agnostic.

Layer 1: scenario fields
- source of truth is scenario JSON fields (see `docs/scenario_schema.md`)

Layer 2: canonical backend keys
- internal normalized keys passed through `SetParam`
- examples today: `fps`, `jitter_us`, `seed`, `frame_size_bytes`,
  `drop_every_n`, `drop_percent`, `burst_drop`, `reorder`

Layer 3: SDK-native calls
- convert canonical keys into vendor SDK enums/units/API calls

### Recommended mapping table format

Maintain a table in your backend module docs/code comments:

| Scenario path | Canonical key | SDK property/API | Unit conversion | Validation |
| --- | --- | --- | --- | --- |
| `camera.fps` | `fps` | `SetFrameRate(...)` | integer Hz | `>0` |
| `camera.pixel_format` | `pixel_format` | `SetPixelFormat(...)` | enum map | allowed enum |
| `camera.trigger_mode` | `trigger_mode` | `SetTriggerMode(...)` | enum map | allowed enum |
| `camera.trigger_source` | `trigger_source` | `SetTriggerSource(...)` | enum map | allowed enum |
| `camera.trigger_activation` | `trigger_activation` | `SetTriggerActivation(...)` | enum map | allowed enum |
| `camera.network.inter_packet_delay_us` | `inter_packet_delay_us` | SDK transport delay API | us -> sdk unit | `>=0` |
| `camera.roi.width` | `roi_width` | ROI setter | pixels | range/device caps |

Notes:
- prefer explicit per-key conversion helpers over generic string parsing
- fail fast when a key cannot be mapped

## Implementation steps

1. Create a new backend module
- path example: `src/backends/vendor_xxx/`
- add `vendor_camera_backend.hpp/.cpp` implementing `ICameraBackend`

2. Add scenario-to-param translator for the real backend
- keep this similar to `src/backends/sim/scenario_config.cpp`
- goal: deterministic ordered `SetParam` calls + actionable validation errors

3. Wire backend into build system
- add source files to `labops_backends` in `CMakeLists.txt`
- keep proprietary includes/libs behind `LABOPS_ENABLE_REAL_BACKEND`
- when SDK is unavailable, build should still succeed with stub path

4. Add runtime backend selection in runner
- today `src/labops/cli/router.cpp` always uses `SimCameraBackend`
- add a selector so run flow can choose sim vs real backend explicitly
- set `run_info.config.backend` to the actual backend used

5. Preserve evidence contracts
- do not change event names or artifact filenames
- ensure CONFIG_APPLIED payload still emits deterministic `param.<key>` fields

6. Add tests
- backend interface smoke test for real adapter contract
- mapping test for scenario->canonical->SDK translation (success + failure)
- failure-path test for connect/start/pull errors with actionable text

7. Keep secrets/proprietary bits out of repo
- do not commit vendor headers, binaries, license files, or confidential docs
- document local SDK setup separately (internal-only docs outside this repo)

## What to verify before merge

Build checks:
- `cmake -S . -B build -DLABOPS_ENABLE_REAL_BACKEND=OFF`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

Real-backend-flag checks (without proprietary SDK code in repo):
- `cmake -S . -B build-real-on -DLABOPS_ENABLE_REAL_BACKEND=ON`
- `cmake --build build-real-on`
- `ctest --test-dir build-real-on --output-on-failure -R sdk_stub_backend_smoke`

Runtime checks (when real SDK integration is available in your environment):
- `./build/labops validate <real-backend-scenario.json>`
- `./build/labops run <real-backend-scenario.json> --out out-real/`
- confirm bundle includes standard artifacts and realistic stream events

## Integration done when

- backend class fully implements `ICameraBackend`
- scenario mapping is explicit, validated, and documented
- build passes with real backend `OFF` and `ON`
- evidence artifacts/events remain contract-compatible
- failure modes return actionable errors, not silent no-op behavior
