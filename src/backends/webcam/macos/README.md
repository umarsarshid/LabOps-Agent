# src/backends/webcam/macos

macOS-specific webcam backend components.

## Why this folder exists

macOS webcam capture will use AVFoundation when implemented. Isolating macOS plumbing here keeps shared backend logic clean and portable.

## Current contents

- `platform_probe_macos.hpp/.cpp`: macOS availability stub for webcam backend.
  - currently reports `BACKEND_NOT_AVAILABLE` reason until AVFoundation
    integration is added.

## Connection to the project

The shared webcam backend routes through this module on macOS to publish platform status and future capability support.
