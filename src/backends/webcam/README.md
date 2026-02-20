# src/backends/webcam

Cross-platform webcam backend scaffold for direct local camera testing.

## Why this folder exists

LabOps already has a deterministic sim backend and a vendor-SDK-oriented real backend path. This folder adds a second real-hardware track focused on commodity webcams so teams can test the same run/metrics/bundle pipeline on a single machine without proprietary camera SDK setup.

## Current scope

- `webcam_backend.hpp/.cpp`:
  - implements `ICameraBackend` with OpenCV bootstrap capture support.
  - opens a selected device index, attempts requested width/height/fps/fourcc,
    and captures requested-vs-actual readback in `dump_config()`.
  - emits frame samples as `received`, `timeout`, or `incomplete` outcomes
    using a wall-clock pull budget.
- `webcam_factory.hpp/.cpp`:
  - centralizes backend registration hooks used by CLI backend routing.
  - reports availability (`compiled`, `available`, `reason`) for
    `labops list-backends` so operators can see why webcam is unavailable on a
    given platform/build.
  - returns a backend instance only when webcam path is compiled for the
    current target.
- `capabilities.hpp/.cpp`:
  - defines a small capability model (`unsupported`, `best_effort`,
    `supported`) for key control families (exposure/gain/pixel-format/ROI/
    trigger/frame-rate).
- `device_model.hpp/.cpp`:
  - defines normalized webcam inventory/control types:
    - `WebcamDeviceInfo` (`device_id`, `friendly_name`, optional `bus_info`)
    - `WebcamControlId` (width/height/fps/pixel_format/exposure/gain/etc.)
    - `SupportedControls` map (`control_id -> spec`) where omitted keys are
      explicitly treated as unsupported.
  - includes JSON-friendly serializers used for capability evidence artifacts.
- `device_selector.hpp/.cpp`:
  - parses webcam selector clauses (`id`, `index`, `name_contains`) with
    actionable errors.
  - enumerates webcam inventories from either:
    - `LABOPS_WEBCAM_DEVICE_FIXTURE` (deterministic CI/dev path), or
    - OpenCV index probing (`0..LABOPS_WEBCAM_MAX_PROBE_INDEX`, default `8`)
      when fixture is not provided.
  - fixture CSV supports optional `capture_index` so selector tests can target
    deterministic open indices without depending on real attached hardware.
  - resolves selectors deterministically using stable ordering:
    - `id` exact match
    - otherwise `index`
    - otherwise `name_contains`
    - otherwise default index `0`
  - emits selection-rule labels (`id`, `index`, `name_contains`,
    `default_index_0`) so CLI logs and run artifacts explain *why* one webcam
    was chosen.
- `opencv_bootstrap.hpp/.cpp`:
  - small build-gated OpenCV bootstrap status module.
  - when `LABOPS_ENABLE_WEBCAM_OPENCV` is effective, compiles with OpenCV and
    records OpenCV version/status in backend config evidence.
  - when disabled or unavailable, provides stable `disabled` status without
    requiring any OpenCV dependency.
- `opencv_webcam_impl.hpp/.cpp`:
  - thin OpenCV wrapper behind `WebcamBackend`.
  - isolates `VideoCapture` open/close, property set/readback, fourcc handling,
    frame acquisition loop, and index probing logic.
  - includes a deterministic test mode (`IWebcamFrameProvider`) so frame
    outcome classification can be validated in CI without camera access.
  - classifies frame outcomes to match real-backend semantics:
    - `TIMEOUT`: no frame within timeout window
    - `INCOMPLETE`: frame arrived with invalid/empty payload
- `capture_clock.hpp/.cpp`:
  - monotonic timestamp bridge used by webcam capture internals.
  - converts internal `steady_clock` capture times into
    contract-compatible `system_clock` timestamps for events/metrics artifacts.
- `platform_probe.hpp/.cpp`:
  - central dispatcher that picks the current OS probe implementation.
- `linux/`, `macos/`, `windows/`:
  - platform-specific availability probes that currently report clear
    unavailability reasons.
- `testing/`:
  - deterministic webcam test providers used by `opencv_webcam_impl` test mode.

## Connection to the project

This module is now operational through an OpenCV bootstrap path. It lets teams
run real local webcam streams through the same LabOps run pipeline (events,
metrics, bundles, summaries) while preserving fixture-driven deterministic
selector behavior for CI and no-hardware environments.

## Build flag notes

- `LABOPS_ENABLE_WEBCAM_OPENCV` controls OpenCV bootstrap compilation.
- Default policy:
  - local: ON
  - CI: OFF
- If requested but OpenCV is not installed, the build falls back to
  non-OpenCV webcam scaffolding and reports bootstrap as disabled.
