# tests/core

Core contract unit tests powered by Catch2.

## Why this folder exists

Core JSON contracts are consumed by multiple modules and external tooling.
These tests lock down serialization behavior so changes are intentional and
reviewable.

## Current contents

- `schema_json_test.cpp`: verifies `RunConfig`/`RunInfo` JSON serialization.
- `event_json_test.cpp`: verifies `EventType`/`Event` JSON serialization.

## Connection to the project

If core JSON formats drift unexpectedly, artifact parsing, event analytics, and
automated triage can silently break. These tests provide an early guardrail.
