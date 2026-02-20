# src/backends/webcam/testing

Webcam backend testing support files.

## Why this folder exists

Upcoming webcam commits need platform-independent tests for capability reporting, parameter handling, and frame-loop behavior. This folder is reserved for small fakes/fixtures so unit tests do not depend on physical webcams.

## Current state

- `mock_frame_provider.hpp/.cpp`:
  - deterministic scripted `IWebcamFrameProvider` implementation used by
    webcam-impl test mode.
  - supports explicit per-frame outcomes (`received`, `timeout`,
    `incomplete`, `dropped`) and optional stall-period gaps.
  - allows CI/local tests to validate frame classification behavior without
    depending on OpenCV camera access.

## Connection to the project

Keeps webcam test infrastructure close to backend code while preserving clean separation from production platform implementations.
