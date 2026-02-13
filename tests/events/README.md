# tests/events

Event model and writer smoke tests.

## Why this folder exists

Timeline events are core triage evidence. These tests protect the event
contract and append-only JSONL writer behavior from regressions.

## Current contents

- `events_jsonl_smoke.cpp`: verifies append behavior and required keys in
  `events.jsonl` lines.

## Connection to the project

If events are malformed or missing, metric derivation and root-cause analysis
lose critical signal. This folder keeps that signal reliable.
