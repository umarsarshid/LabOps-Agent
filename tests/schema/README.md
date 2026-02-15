# tests/schema

Schema-focused smoke/regression tests.

## Why this folder exists

Schema drift can silently break automation and downstream tooling. These tests
validate that core contracts remain serializable and preserve required fields.

## Current contents

- `run_contract_json_smoke.cpp`: checks `RunConfig` and `RunInfo` JSON output
  includes required keys, optional real-device metadata fields, and timestamp
  fields.

## Connection to the project

Stable schema contracts are required for reproducible runs, artifact parsing,
and reliable triage packets.
