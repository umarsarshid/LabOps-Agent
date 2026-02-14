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

## Current contents

- `system_probe.hpp/.cpp`:
  - collects lightweight host snapshot fields used by triage:
    - OS name/version
    - CPU model/logical core count
    - total RAM bytes
    - uptime seconds
    - load snapshot (`1m/5m/15m` when platform supports it)
  - collects raw NIC command outputs:
    - Windows: `ipconfig /all`
    - Linux: `ip a`, `ip r`, `ethtool` (if available)
    - macOS: `ifconfig -a`, `netstat -rn`, `route -n get default`
  - serializes snapshot into stable JSON contract for bundle artifacts.

## Design principle

Probe collection should be lightweight, non-invasive, and deterministic where possible.

## Connection to the project

Host context is key evidence in the engineer packet and helps explain why failures may reproduce in one environment but not another.
