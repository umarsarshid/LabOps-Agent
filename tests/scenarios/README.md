# tests/scenarios

Scenario contract and scenario-driven integration smoke tests.

## Why this folder exists

Scenario files drive repeatability. These tests ensure:
- invalid inputs fail with clear, actionable messages before runtime execution
- baseline scenarios can run end-to-end and produce sane metric ranges

## Current contents

- `scenario_validation_smoke.cpp`: validates core schema checks and parse error
  messaging for `labops validate`, including optional `netem_profile`
  reference checks, optional backend enum checks
  (`sim` / `webcam` / `real_stub`), real-backend `device_selector` contract
  checks (syntax, allowed keys, backend compatibility), and additive webcam
  section checks (`webcam.device_selector` + requested width/height/fps/
  pixel-format fields).
- `sim_baseline_metrics_integration_smoke.cpp`: runs
  `scenarios/sim_baseline.json` through `labops run` and validates
  `<out>/<run_id>/metrics.json` stays within expected baseline ranges
  (FPS/drop/timing/jitter).

## Connection to the project

If scenario validation is weak, automated triage becomes noisy and non-repeatable.
