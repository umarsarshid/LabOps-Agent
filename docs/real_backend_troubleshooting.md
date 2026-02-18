# Real Backend Troubleshooting (AE/QA Notes)

Use this page when a real-camera run is failing and you need a fast, practical
triage path.

Scope:
- `backend: real_stub` runs with real-backend plumbing enabled
- lab setup and transport behavior
- trigger and timing confusion

Goal:
- identify the failure class quickly
- collect enough evidence for a useful handoff
- avoid blind trial-and-error

## Quick Triage Flow (5-10 minutes)

1. Confirm backend state:
   - `./build/labops list-backends`
   - expected for real path: `real ✅ enabled`
2. Confirm device visibility:
   - `./build/labops list-devices --backend real`
3. Run manual smoke once:
   - `cmake --build build --target real_camera_smoke_manual`
4. Inspect latest bundle under:
   - `tmp/manual_real_camera_smoke/<run_id>/`
5. Check these files first:
   - `run.json`
   - `config_verify.json`
   - `config_report.md`
   - `events.jsonl`
   - `metrics.json`
   - `summary.md`

If smoke already fails, do not start deep scenario tuning yet. Fix lab/setup
first.

## Symptom: Device Not Found

Typical signs:
- `list-devices` shows no cameras
- run fails before stream start
- errors mention selector mismatch or discovery failure

### Most likely causes

- Real backend is effectively disabled (SDK not discovered).
- Wrong camera selector (`serial`, `user_id`, or `index`).
- Camera is powered but not reachable on current host/network path.
- Fixture CSV is malformed when using `LABOPS_REAL_DEVICE_FIXTURE`.

### What to check

1. Backend and SDK discovery:
   - `./build/labops list-backends`
   - if it says disabled, reconfigure CMake with valid SDK include/lib paths.
2. Discovery output:
   - `./build/labops list-devices --backend real`
3. Selector syntax:
   - valid: `serial:SN-1234`
   - valid: `user_id:LineCam,index:0`
   - invalid: mixed selector with missing values or duplicate keys
4. If using fixture CSV:
   - each row needs at least: `model,serial,user_id,transport`

### Fix pattern

- Start with no selector and confirm at least one camera is discovered.
- Then pin a selector explicitly:
  - `./build/labops run <scenario.json> --out tmp/runs --device serial:SN-1234`
- If multiple cameras share `user_id`, add `index:<n>`.

### Evidence to attach

- console output from `list-backends` and `list-devices`
- failing `scenario.json`
- `run.json` (if created)
- exact selector used

## Symptom: Timeouts / Intermittent Frame Gaps

Typical signs:
- many `FRAME_TIMEOUT` events
- `metrics.json` shows elevated timeout counters
- average FPS below expectation

### Most likely causes

- Transport congestion or packet loss.
- Over-aggressive frame rate/exposure combination.
- Host resource pressure (CPU/load spikes).
- Camera/network settings partially applied or clamped.

### What to check

1. Event timeline:
   - `events.jsonl` for `FRAME_TIMEOUT`, `DEVICE_DISCONNECTED`
2. Metrics counters:
   - `timeout_frames_total`
   - `drop_rate_percent`
   - jitter and inter-frame p95 values
3. Applied-vs-actual config:
   - `config_verify.json`
   - `config_report.md`
4. Host/network context:
   - `hostprobe.json`
   - `nic_*.txt`

### Fix pattern

- Run same scenario in free-run with conservative FPS first.
- Lower FPS and retest.
- If GigE, reduce packet stress and review MTU/packet-size alignment.
- Compare failing run against baseline:
  - `./build/labops compare --baseline baselines/<scenario_id> --run <run_dir>`

### Evidence to attach

- `events.jsonl`
- `metrics.json`
- `diff.json` / `diff.md` (if compared)
- `hostprobe.json` + NIC raw files

## Symptom: Trigger Confusion (No Frames, Bursts, or Wrong Timing)

Typical signs:
- stream starts but frames are sparse or absent
- behavior changes unexpectedly between free-run and triggered scenarios
- trigger settings appear set, but camera acts differently

### Most likely causes

- Trigger mode/source/activation mismatch.
- External trigger signal not present or edge polarity mismatch.
- Settings requested but adjusted/unsupported by device.

### What to check

1. Requested vs actual trigger fields in:
   - `config_verify.json`
   - keys: `trigger_mode`, `trigger_source`, `trigger_activation`
2. Human-readable status table:
   - `config_report.md`
   - look for adjusted/unsupported rows
3. Stream events and cadence:
   - `events.jsonl`

### Fix pattern

- Establish baseline in `trigger_mode=free_run`.
- Change one trigger variable at a time:
  - mode, then source, then activation.
- Re-run and compare after each change.
- If trigger is external, verify signal/polarity independently outside LabOps.

### Evidence to attach

- scenario used
- trigger-related rows from `config_report.md`
- short `events.jsonl` excerpt showing timing pattern

## Symptom: GigE MTU / Packet Size Issues

Typical signs:
- high resend/packet-error counters
- dropped/timeout spikes under load
- unstable FPS at higher throughput

### Most likely causes

- MTU mismatch along path (camera, NIC, switch).
- Packet size too large for actual network path.
- Inter-packet delay too low for host/switch capacity.

### What to check

1. Transport identity:
   - `run.json` real device transport should be `gige`
2. Network tuning apply/readback:
   - `config_verify.json`
   - keys: `packet_size_bytes`, `inter_packet_delay_us`
3. Transport counters:
   - `run.json` transport counters (`resends`, `packet_errors`, `dropped_packets`)
4. Host NIC evidence:
   - `hostprobe.json`
   - `nic_*.txt` outputs

### Fix pattern

- Start conservative:
  - smaller packet size
  - non-zero inter-packet delay
- Increase throughput gradually while watching counters and timeout rate.
- Keep one-variable-at-a-time changes so root cause stays clear.

### Notes

- For non-GigE transports, network tuning keys may be unsupported by design.
- In best-effort mode, unsupported keys should be reported, not silently ignored.

## Escalation Packet Checklist

When handing off to platform/firmware/network teams, include:
- scenario file used
- command used (exact CLI)
- run bundle path
- failure timestamp window
- selector/device identity (model/serial/transport)
- top 2-3 anomalies from `summary.md`
- `config_report.md` and `config_verify.json`
- `events.jsonl` and `metrics.json`
- NIC evidence files

This is usually enough for another engineer to reproduce without a long
back-and-forth thread.

## Related Docs

- `docs/real_backend_setup.md`
- `docs/scenario_schema.md`
- `docs/triage_bundle_spec.md`
- `docs/integration/real_sdk_backend.md`
