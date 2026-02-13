# src/metrics

`src/metrics/` computes performance metrics from runtime data.

## Why this folder exists

Engineers need objective numbers, not only logs. This module turns event traces and frame timing into consistent measurements such as FPS, jitter, dropped-frame rate, and disconnect windows.

## Expected responsibilities

- Metric definitions and formulas.
- Aggregation windows and summary generation.
- Baseline-comparison-ready outputs.

## Current contents

- `fps.hpp/.cpp`: computes average FPS over the run window and rolling FPS over
  a fixed time window, using received frames only. Also computes:
  - drop stats (`total dropped`, `drop rate percent`)
  - inter-frame interval stats (`min/avg/p95`) in microseconds
  - inter-frame jitter stats (`min/avg/p95`) in microseconds
- Serialization of metric artifacts (`metrics.csv` and `metrics.json`) lives in
  `src/artifacts/` so output contracts stay centralized.

## Design principle

Metric math must be deterministic and versionable. If formulas change, we should be able to explain differences between old and new runs.

## Connection to the project

Metrics are a core part of pass/fail decisions, regression detection, and the evidence included in engineer packets.
