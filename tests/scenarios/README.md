# tests/scenarios

Scenario validation smoke tests.

## Why this folder exists

Scenario files drive repeatability. These tests ensure invalid inputs fail with
clear, actionable messages before runtime execution begins.

## Current contents

- `scenario_validation_smoke.cpp`: validates core schema checks and parse error
  messaging for `labops validate`.

## Connection to the project

If scenario validation is weak, automated triage becomes noisy and non-repeatable.
