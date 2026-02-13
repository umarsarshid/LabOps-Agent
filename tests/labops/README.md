# tests/labops

CLI-level integration smoke tests.

## Why this folder exists

Some behaviors are only meaningful when exercised through the `labops` command
surface (argument parsing + runtime orchestration + artifact/event output).

## Current contents

- `run_stream_trace_smoke.cpp`: drives `labops run` via CLI dispatch and
  verifies `events.jsonl` contains realistic stream lifecycle events.

## Connection to the project

These tests validate the user-facing contract, not just individual modules,
which is critical for operator trust and CI confidence.
