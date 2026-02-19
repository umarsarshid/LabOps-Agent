# src/backends/webcam

Cross-platform webcam backend scaffold for direct local camera testing.

## Why this folder exists

LabOps already has a deterministic sim backend and a vendor-SDK-oriented real backend path. This folder adds a second real-hardware track focused on commodity webcams so teams can test the same run/metrics/bundle pipeline on a single machine without proprietary camera SDK setup.

## Current scope (commit 0097)

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
- `platform_probe.hpp/.cpp`:
  - central dispatcher that picks the current OS probe implementation.
- `linux/`, `macos/`, `windows/`:
  - platform-specific availability probes that currently report clear
    unavailability reasons.
- `testing/`:
  - reserved for future webcam backend test fixtures and platform fakes.

## Connection to the project

This module is intentionally non-operational right now, but it establishes the backend contract and capability vocabulary needed for the next commits where Linux V4L2 and Windows Media Foundation capture paths are wired in.
