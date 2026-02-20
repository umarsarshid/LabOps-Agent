# tests/manual

Manual developer-only integration targets that are intentionally excluded from
CI automation.

## Why this folder exists

Hardware bring-up checks should be runnable in one command, but they should not
block CI (which usually has no cameras attached).

## Current contents

- `real_camera_smoke_scenario.json`: 5-second real-backend smoke scenario used
  by the CMake manual target.
- `webcam_linux_smoke_scenario.json`: 5-second Linux webcam smoke scenario
  used by the Linux V4L2 manual target.

## One-command manual smoke flow

From the repository root:

```bash
cmake --build build --target real_camera_smoke_manual
```

This runs:

- `labops run tests/manual/real_camera_smoke_scenario.json --out tmp/manual_real_camera_smoke`

and validates the lab path:

- connect
- stream for 5 seconds
- write run artifacts (including config dump)
- exit

Linux webcam (native V4L2) one-command smoke:

```bash
cmake --build build --target webcam_linux_smoke_manual
```

This runs:

- `labops run tests/manual/webcam_linux_smoke_scenario.json --out tmp/manual_webcam_linux_smoke`

and validates the Linux webcam path:

- connect
- stream for 5 seconds
- write run artifacts (including events/metrics/config report)
- exit

## Output location

- `tmp/manual_real_camera_smoke/<run_id>/`
- `tmp/manual_webcam_linux_smoke/<run_id>/`

## Notes

- This target is not registered with `ctest`.
- For multi-camera labs, run the equivalent CLI command directly with
  `--device <selector>` to pin a specific camera.
- `webcam_linux_smoke_manual` only executes a real run on Linux; on non-Linux
  hosts the target prints a clear "Linux-only" message.
