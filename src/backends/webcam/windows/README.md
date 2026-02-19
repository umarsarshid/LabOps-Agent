# src/backends/webcam/windows

Windows-specific webcam backend components.

## Why this folder exists

Windows webcam capture will use Media Foundation when implemented. Keeping it in a dedicated folder prevents platform-specific includes and lifecycle code from leaking into shared backend files.

## Current contents

- `platform_probe_windows.hpp/.cpp`: Windows availability stub for webcam backend.
  - currently reports `BACKEND_NOT_AVAILABLE` reason until Media Foundation
    capture wiring is added.

## Connection to the project

The shared webcam backend uses this module to expose Windows availability now and capability signals later.
