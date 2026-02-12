# src/hostprobe

`src/hostprobe/` captures host environment context for each run.

## Why this folder exists

Camera performance issues are often host-dependent (OS version, driver, NIC settings, CPU contention). Capturing this context up front reduces back-and-forth during triage.

## Expected responsibilities

- Collect machine metadata (OS, CPU, memory, network basics, clock info).
- Normalize host facts into run metadata.
- Flag missing/unknown probe fields safely.

## Design principle

Probe collection should be lightweight, non-invasive, and deterministic where possible.

## Connection to the project

Host context is key evidence in the engineer packet and helps explain why failures may reproduce in one environment but not another.
