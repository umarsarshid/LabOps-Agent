# src/hostprobe

`src/hostprobe/` captures host environment context for each run.

## Why this folder exists

Camera performance issues are often host-dependent (OS version, driver, NIC settings, CPU contention). Capturing this context up front reduces back-and-forth during triage.

## Expected responsibilities

- Collect machine metadata (OS, CPU, memory, uptime, load snapshot).
- Collect NIC command output evidence (best-effort, platform-specific).
- Parse stable NIC highlights for quick triage (`default route`, interfaces,
  address presence, MTU, link speed hints) from raw command output.
- Normalize host facts into run metadata.
- Flag missing/unknown probe fields safely.
- Provide optional identifier redaction helpers for shareable bundles.

## Current contents

- `system_probe.hpp`:
  - public hostprobe API used by the CLI and artifact writers.
- `system_probe_internal.hpp`:
  - internal cross-file contracts for shared parsing helpers and
    platform hook points.
- `system_probe_common.cpp`:
  - platform-agnostic logic:
    - JSON serialization
    - redaction token build/apply
    - command capture helpers
    - shared NIC parsers
    - orchestration that calls platform hooks
- `system_probe_linux.cpp`:
  - Linux-only host + NIC probe implementations.
- `system_probe_macos.cpp`:
  - macOS-only host + NIC probe implementations.
- `system_probe_windows.cpp`:
  - Windows-only host + NIC probe implementations.

All files above preserve the same observable output contract as before
(`hostprobe.json` plus raw NIC command evidence); this split only improves
maintainability and platform isolation.

## Design principle

Probe collection should be lightweight, non-invasive, and deterministic where possible.

## Connection to the project

Host context is key evidence in the engineer packet and helps explain why failures may reproduce in one environment but not another.
