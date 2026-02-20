# src/backends/webcam/linux

Linux-specific webcam backend components.

## Why this folder exists

Linux webcam support will be implemented using V4L2 (`/dev/video*`). Keeping Linux code isolated avoids mixing platform APIs inside shared backend orchestration.

## Current contents

- `platform_probe_linux.hpp/.cpp`: Linux availability stub for webcam backend.
  - reports Linux webcam backend availability status.
- `v4l2_device_enumerator.hpp/.cpp`:
  - native Linux webcam discovery implementation.
  - scans `/dev/video*` character devices.
  - uses `VIDIOC_QUERYCAP` to read camera name/capabilities.
  - maps discovered cameras into normalized `WebcamDeviceInfo` rows used by
    `list-devices` and selector resolution.

## Connection to the project

The shared `WebcamBackend` asks this module what Linux can do, then surfaces that status through standard backend errors and config dumps.
