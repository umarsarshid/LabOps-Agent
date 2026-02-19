# src/backends

`src/backends/` contains camera execution backends.

## Why this folder exists

The project needs one consistent workflow that can run on different camera stacks. Backends provide an adapter layer so the agent can keep the same logic while hardware integrations vary.

## Expected responsibilities

- Shared backend interfaces/contracts.
- Backend selection and capability mapping.
- Isolated implementations under backend-specific subfolders.

## Current contents

- `camera_backend.hpp`: `ICameraBackend` contract (`connect/start/stop`,
  `set_param`, `dump_config`, `pull_frames(duration)`).
  - `FrameSample` now includes `frame_id`, `timestamp`, `size_bytes`, and
    optional `dropped`.
- `real_sdk/`: real-backend skeleton + factory wiring.
  - includes `NodeMapAdapter` abstraction used for generic-key capability
    checks and future SDK node mapping before camera apply calls are executed.
  - includes `ParamKeyMap` loading (`maps/param_key_map.json`) so generic
    LabOps keys can be remapped to vendor node names without editing core
    orchestration code.
  - includes strict/best-effort real-parameter application that uses both the
    key map and node adapter, enabling deterministic unsupported/adjusted
    reporting in run events and readback verification (`requested vs actual vs supported`)
    in bundle evidence (`config_verify.json`, `camera_config.json`,
    `config_report.md`).
  - `RealBackend` implements `ICameraBackend` with deterministic placeholder
    behavior until vendor SDK frame adapter calls are integrated.
  - real acquisition lifecycle now runs through a dedicated stream-session RAII
    guard so start/stop stays safe in error paths and stop remains idempotent.
  - current OSS loop emits deterministic frame outcomes (`received`, `timeout`,
    `incomplete`) so run artifacts/metrics remain meaningful without hardware.
  - `SdkContext` provides one-time process SDK init/shutdown via RAII so
    startup/teardown are safe across repeated runs and tests.
  - `DeviceInfo` + discovery mapping normalize SDK camera descriptors into
    stable fields (`model`, `serial`, `user_id`, `transport`, `ip`, `mac`) for
    CLI visibility and downstream evidence capture, including optional version
    fields (`firmware_version`, `sdk_version`) when discovery sources expose
    them.
  - device selector parsing/resolution supports deterministic camera choice by
    `serial`, `user_id`, and optional `index` tie-breaks so repeated runs pick
    the same physical device.
  - includes best-effort transport-counter collection normalization for
    `resends`, `packet_errors`, and `dropped_packets` so run metadata can
    report values when exposed by SDK nodes and explicit not-available status
    otherwise.
  - `CreateRealBackend()` selects the effective implementation based on build
    availability (real skeleton when enabled, sdk stub fallback otherwise).
- `sim/`: deterministic in-repo implementation of the contract with
  scenario-controlled fault knobs.
- `sdk_stub/`: non-proprietary real-backend integration boundary.
  - includes `RealCameraBackendStub` so builds can compile without vendor SDKs.
  - controlled by `LABOPS_ENABLE_REAL_BACKEND` build flag (default `OFF`).
- `webcam/`: cross-platform webcam backend scaffold.
  - adds a standalone `WebcamBackend` implementing `ICameraBackend`.
  - adds `webcam_factory` for backend registry/status wiring so CLI can report
    webcam availability + reason.
  - introduces a capability model (`unsupported`, `best_effort`, `supported`)
    for key webcam controls (`exposure`, `gain`, `pixel_format`, `roi`,
    `trigger`, `frame_rate`).
  - includes per-platform availability probes under `linux/`, `macos/`, and
    `windows/` with explicit `BACKEND_NOT_AVAILABLE` reasons until capture
    loops are implemented.

## Current and planned backends

- `sim/`: deterministic simulator for development and CI.
- `real_sdk/`: SDK-enabled real-backend skeleton and object factory.
- `sdk_stub/`: integration-ready stub path for proprietary camera SDKs.
- `webcam/`: direct webcam backend path (Linux V4L2 / macOS AVFoundation /
  Windows Media Foundation) scaffolded for incremental implementation.

## Connection to the project

This folder is what makes "same autopilot brain, different camera" possible.
