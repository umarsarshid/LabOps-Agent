# tests/metrics

Metrics-focused smoke tests.

## Why this folder exists

Metric math directly affects engineering decisions. These tests lock down
formula behavior and output artifacts so regressions are caught early.

## Current contents

- `fps_metrics_smoke.cpp`: validates average + rolling FPS computation and
  verifies `metrics.csv` emission contract (`avg_fps` and timing/jitter rows).
- `jitter_injection_smoke.cpp`: verifies injected sim jitter changes computed
  timing metrics (`jitter avg` and `interval p95` increase with higher jitter).

## Connection to the project

Metrics are the quantitative evidence in triage packets. If metric outputs
drift unexpectedly, diagnosis quality drops quickly.
