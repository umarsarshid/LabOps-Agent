# src/scenarios

`src/scenarios/` handles scenario definitions and validation.

## Why this folder exists

A repeatable lab depends on declarative scenario files. This module ensures scenarios are validated and interpreted the same way every time.

## Expected responsibilities

- Scenario schema and defaults.
- File loading and validation errors.
- Normalized scenario objects passed to executors.

## Current contents

- `model.hpp/.cpp`: typed runtime parse model (`ScenarioModel`) used by
  run planning. This parser supports canonical schema paths plus legacy
  flat keys in one place so runtime behavior stays backward compatible, and
  now includes additive optional webcam request fields
  (`webcam.device_selector`, requested width/height/fps/pixel-format).
- `validator.hpp/.cpp`: scenario JSON loader + schema validation with
  actionable error messages keyed by field path (used by `labops validate`),
  including `netem_profile` reference checks against
  `tools/netem_profiles/<profile>.json`.
  - validates webcam selector constraints (`index`/`id`/`name_contains`) and
    enforces backend compatibility (`webcam.device_selector` requires
    `backend: "webcam"`).
- `netem_profile_support.hpp/.cpp`: shared slug/path helpers for
  `netem_profile` lookup so runtime (`labops run`) and validation
  (`labops validate`) use the same resolution rules.

## Design principle

Fail early with clear validation messages. Invalid scenarios should never reach runtime execution.

## Connection to the project

Scenario correctness is the foundation for reproducibility. If scenario parsing is inconsistent, all downstream metrics and triage conclusions become unreliable.
