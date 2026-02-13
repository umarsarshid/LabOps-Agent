# scenarios

Scenario files used by CLI runs and validation.

## Why this folder exists

This directory is the source of truth for runnable test definitions. Scenarios should be versioned, reviewable, and easy to diff.

## Expected contents

- Baseline scenarios (known-good behavior).
- Failure-injection scenarios (drops, jitter, disconnects).
- Targeted investigation scenarios (trigger/ROI/pixel-format sweeps).
- `sim_baseline.json`: stable known-good reference scenario.
- `dropped_frames.json`: deterministic drop/reorder fault reproduction scenario.
- `trigger_roi.json`: trigger + ROI starter scenario for targeted investigations.
- `foo.json`: minimal sample scenario matching the documented schema.

## Authoring guidance

- Keep scenarios declarative.
- Prefer explicit values over hidden defaults.
- Name scenarios by intent so run history remains searchable.
- Follow `docs/scenario_schema.md` for required fields and constraints.

## Connection to the project

Scenarios define what gets executed. They are the first input to reproducible testing and automated triage.
