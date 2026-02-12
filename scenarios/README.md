# scenarios

Scenario files used by CLI runs and validation.

## Why this folder exists

This directory is the source of truth for runnable test definitions. Scenarios should be versioned, reviewable, and easy to diff.

## Expected contents

- Baseline scenarios (known-good behavior).
- Failure-injection scenarios (drops, jitter, disconnects).
- Targeted investigation scenarios (trigger/ROI/pixel-format sweeps).

## Authoring guidance

- Keep scenarios declarative.
- Prefer explicit values over hidden defaults.
- Name scenarios by intent so run history remains searchable.

## Connection to the project

Scenarios define what gets executed. They are the first input to reproducible testing and automated triage.
