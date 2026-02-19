# tests/events

Event model and writer smoke tests.

## Why this folder exists

Timeline events are core triage evidence. These tests protect the event
contract and append-only JSONL writer behavior from regressions.

## Current contents

- `emitter_smoke.cpp`: verifies typed emitter facade payload contracts,
  including explicit config-status helper emission for `CONFIG_APPLIED`,
  `CONFIG_UNSUPPORTED`, and `CONFIG_ADJUSTED`.
- `events_jsonl_smoke.cpp`: verifies append behavior and required keys in
  `events.jsonl` lines, including lifecycle-critical event type serialization
  (`DEVICE_DISCONNECTED`, `TRANSPORT_ANOMALY`).
- `transport_anomaly_smoke.cpp`: verifies optional transport-counter anomaly
  heuristics trigger findings only when counters are available and exceed
  thresholds.

## Connection to the project

If events are malformed or missing, metric derivation and root-cause analysis
lose critical signal. This folder keeps that signal reliable.
