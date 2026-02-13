# tests/metrics

Metrics-focused smoke tests.

## Why this folder exists

Metric math directly affects engineering decisions. These tests lock down
formula behavior and output artifacts so regressions are caught early.

## Current contents

- `fps_metrics_smoke.cpp`: validates average + rolling FPS computation and
  verifies `metrics.csv` emission contract (`avg_fps` row present).

## Connection to the project

Metrics are the quantitative evidence in triage packets. If metric outputs
drift unexpectedly, diagnosis quality drops quickly.
