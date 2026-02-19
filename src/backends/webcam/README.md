# src/backends/webcam

Cross-platform webcam backend scaffold for direct local camera testing.

## Why this folder exists

LabOps already has a deterministic sim backend and a vendor-SDK-oriented real backend path. This folder adds a second real-hardware track focused on commodity webcams so teams can test the same run/metrics/bundle pipeline on a single machine without proprietary camera SDK setup.

## Current scope

- `webcam_backend.hpp/.cpp`:
  - implements `ICameraBackend` with stable state transitions and explicit
    `BACKEND_NOT_AVAILABLE` failures until platform capture loops are added.
  - preserves requested params in `dump_config()` so early wiring can still be
    inspected in artifacts/tests.
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
  - enumerates fixture-driven webcam inventories via
    `LABOPS_WEBCAM_DEVICE_FIXTURE` so deterministic selector behavior is
    testable without physical webcams in CI.
  - resolves selectors deterministically using stable ordering:
    - `id` exact match
    - otherwise `index`
    - otherwise `name_contains`
    - otherwise default index `0`
  - emits selection-rule labels (`id`, `index`, `name_contains`,
    `default_index_0`) so CLI logs and run artifacts explain *why* one webcam
    was chosen.
- `platform_probe.hpp/.cpp`:
  - central dispatcher that picks the current OS probe implementation.
- `linux/`, `macos/`, `windows/`:
  - platform-specific availability probes that currently report clear
    unavailability reasons.
- `testing/`:
  - reserved for future webcam backend test fixtures and platform fakes.

## Connection to the project

This module is intentionally non-operational for frame streaming right now, but
it already provides deterministic webcam selection and identity reporting. That
lets teams validate end-to-end run orchestration and evidence contracts before
platform capture loops (Linux V4L2 / macOS AVFoundation / Windows Media
Foundation) are wired in.
