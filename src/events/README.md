# src/events

`src/events/` defines and records runtime event streams.

## Why this folder exists

Debugging camera failures requires timeline evidence, not just summary metrics. Event logs capture what happened and when (stream start, frame timing anomalies, disconnects, retries, warnings).

## Expected responsibilities

- Event schema definitions.
- Event writer(s), typically JSONL append-only output.
- Event normalization so backend-specific signals map to a common model.

## Current contents

- `event_model.hpp/.cpp`: `EventType` and `Event{ts,type,payload}` contract
  plus JSON serialization.
- `jsonl_writer.hpp/.cpp`: append-only writer for `events.jsonl` (one JSON
  object per line).
- `transport_anomaly.hpp/.cpp`: optional transport-counter heuristics that
  convert real-run counter snapshots into structured anomaly findings used by
  both timeline events and summary callouts.

## Current stream trace event types

- `CONFIG_APPLIED`
- `CONFIG_UNSUPPORTED`
- `CONFIG_ADJUSTED`
- `STREAM_STARTED`
- `FRAME_RECEIVED`
- `FRAME_DROPPED`
- `FRAME_TIMEOUT`
- `FRAME_INCOMPLETE`
- `DEVICE_DISCONNECTED`
- `TRANSPORT_ANOMALY` (optional, heuristic-driven)
- `STREAM_STOPPED`

## Key design principle

Events should be machine-readable and stable enough that metrics and agent diagnosis can be derived from them without backend-specific branches everywhere.

## Connection to the project

When failures happen, event streams are the raw evidence used to compute metrics, compare against baselines, and explain cause in the final engineer packet.
