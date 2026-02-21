# Linux Webcam Troubleshooting (AE/QA Notes)

Use this page when a Linux webcam run fails or behaves inconsistently and you
need a fast triage path.

Scope:
- `backend: webcam` runs on Linux hosts
- native V4L2 primary path (`/dev/video*`)
- OpenCV fallback behavior when native path cannot be used

Goal:
- classify failure quickly
- collect enough evidence for a useful handoff
- avoid random tuning loops

## Quick Triage Flow (5-10 minutes)

1. Confirm backend state:
   - `./build/labops list-backends`
   - expected on Linux: `webcam ✅ enabled`
2. Confirm device visibility:
   - `./build/labops list-devices --backend webcam`
3. Run manual Linux webcam smoke once:
   - `cmake --build build --target webcam_linux_smoke_manual`
4. Inspect latest bundle under:
   - `tmp/manual_webcam_linux_smoke/<run_id>/`
5. Check these files first:
   - `run.json`
   - `events.jsonl`
   - `metrics.json`
   - `config_verify.json`
   - `config_report.md`
   - `summary.md`

If smoke fails, fix lab/setup issues before deep scenario tuning.

## Symptom: Permission Denied on `/dev/video*`

Typical signs:
- connect fails with permission-related text
- `list-devices` may work, but `run` fails on connect/start
- errors mention denied/not permitted

### Most likely causes

- user is not allowed to open video devices
- container/sandbox has no device access
- host security policy blocks camera device access

### What to check

1. Device permissions:
   - `ls -l /dev/video*`
2. Current user groups:
   - `id`
3. Process-level access errors in:
   - console output
   - `summary.md`
   - `events.jsonl`

### Fix pattern

- ensure the run user has access to `/dev/video*` (group/policy level)
- if in container, pass through video devices explicitly
- retry manual smoke first, then scenario run

### Evidence to attach

- `ls -l /dev/video*` output
- `id` output
- failing `summary.md` and `run.json`

## Symptom: Busy Device / Camera Already In Use

Typical signs:
- connect/start fails with busy/in-use errors
- run works only after closing another app

### Most likely causes

- another process has opened the webcam
- previous crashed run left camera session active briefly

### What to check

1. Which process holds the device:
   - `lsof /dev/video*`
2. LabOps run lock behavior:
   - `tmp/labops.lock` (camera runs are single-process guarded)
3. Recent failures in:
   - `events.jsonl`
   - `summary.md`

### Fix pattern

- close competing apps/processes (browser, video tools, old test process)
- remove stale conditions, then rerun manual smoke
- if lock exists with active PID, wait/stop that process before retrying

### Evidence to attach

- `lsof /dev/video*` output
- lock file state (`tmp/labops.lock`) if relevant
- failing bundle path

## Symptom: Driver Clamps FPS (Requested != Actual)

Typical signs:
- scenario requests e.g. 60 fps but measured avg is lower
- `config_report.md` shows adjusted rows
- `config_verify.json` readback differs from requested values

### Most likely causes

- camera/driver does not support requested fps at requested format/resolution
- format and fps combination is invalid, driver picked nearest supported
- bandwidth constraints force lower effective frame interval

### What to check

1. Requested vs actual:
   - `config_verify.json`
   - `config_report.md`
2. Effective metrics:
   - `metrics.json` (`avg_fps`, inter-frame p95)
3. Device capability hints:
   - `labops list-devices --backend webcam`

### Fix pattern

- lower one variable at a time:
  - width/height first
  - then fps
  - then pixel format
- compare run to baseline after each change:
  - `./build/labops compare --baseline <baseline_dir> --run <run_dir>`

### Evidence to attach

- `config_verify.json`
- `config_report.md`
- `metrics.json`
- `diff.json` / `diff.md` if compared

## Symptom: Auto Exposure Causes Timing Jitter

Typical signs:
- unstable frame intervals under changing scene brightness
- jitter/interval p95 spikes while drop rate stays low
- run is stable in fixed lighting but unstable under motion/light changes

### Most likely causes

- camera auto-exposure loop changes exposure duration dynamically
- exposure growth reduces effective throughput under low light
- driver policy for auto modes differs from expected behavior

### What to check

1. Scenario settings:
   - requested exposure / auto-exposure related keys
2. Timing metrics:
   - `metrics.json` (`inter_frame_interval_*`, `inter_frame_jitter_*`)
3. Event cadence:
   - `events.jsonl` for bursty timeout/incomplete clusters

### Fix pattern

- run a controlled baseline:
  - fixed lighting
  - conservative fps
- disable/limit auto exposure where supported
- retest with one change at a time and compare against baseline

### Evidence to attach

- scenario JSON used
- `metrics.json`
- `summary.md` anomalies
- short `events.jsonl` excerpt around jitter spike window

## Symptom: Native V4L2 Path Falls Back to OpenCV

Typical signs:
- run succeeds but backend evidence indicates OpenCV fallback
- behavior differs from expected Linux native path

### Most likely causes

- selected device did not support mmap streaming path
- native open/apply/start path failed and fallback was used
- OpenCV fallback compiled and available

### What to check

1. Backend evidence keys in artifacts:
   - `webcam.capture_backend`
   - `webcam.capture_fallback_reason`
   - `webcam.linux_capture.*`
2. Device listing and selector:
   - `./build/labops list-devices --backend webcam`

### Fix pattern

- verify selector targets intended device index/id
- test another webcam index/device to isolate driver issue
- keep OpenCV fallback for continuity, but file issue if native path should work

### Evidence to attach

- `run.json`
- `config_verify.json`
- `config_report.md`
- selector used

## Escalation Packet Checklist

When handing off to platform/driver owners, include:
- exact scenario and command used
- run bundle path
- device identity from `run.json`
- `config_verify.json` and `config_report.md`
- `events.jsonl` and `metrics.json`
- `summary.md`
- host/NIC evidence when transport pressure is suspected

This is usually enough for another engineer to reproduce without a long
back-and-forth thread.

## Related Docs

- `docs/webcam_backend.md`
- `src/backends/webcam/README.md`
- `docs/scenario_schema.md`
- `docs/triage_bundle_spec.md`
