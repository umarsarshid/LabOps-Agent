# src/agent

`src/agent/` implements automated triage experiments.

## Why this folder exists

This module is the "autopilot engineer" layer. It runs disciplined experiments by changing one variable at a time, rerunning scenarios, comparing against baseline, and deciding when cause isolation is strong enough.

## Expected responsibilities

- Experiment planning strategy.
- Controlled parameter mutation (ROI, trigger mode, pixel format, network knobs, etc.).
- Result comparison against known-good baselines.
- Stopping logic (`isolated`, `inconclusive`, `needs_human`).
- Human-readable report/packet generation support.

## Design principle

Prioritize explainability over black-box decisions. Every agent conclusion should include the evidence trail that produced it.

## Connection to the project

This folder turns raw testing into rapid root-cause narrowing, which is the main product value of LabOps.
