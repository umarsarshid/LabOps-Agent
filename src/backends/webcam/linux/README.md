# src/backends/webcam/linux

Linux-specific webcam backend components.

## Why this folder exists

Linux webcam support will be implemented using V4L2 (`/dev/video*`). Keeping Linux code isolated avoids mixing platform APIs inside shared backend orchestration.

## Current contents

- `platform_probe_linux.hpp/.cpp`: Linux availability stub for webcam backend.
  - currently reports `BACKEND_NOT_AVAILABLE` reason until V4L2 capture code is
    added.

## Connection to the project

The shared `WebcamBackend` asks this module what Linux can do, then surfaces that status through standard backend errors and config dumps.
