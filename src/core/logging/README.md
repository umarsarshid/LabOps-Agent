# src/core/logging

Shared structured logging primitives used by CLI/runtime code.

## Why this folder exists

LabOps needs logs that are easy to read, easy to filter, and easy to
correlate with run artifacts. A shared logger avoids each module inventing its
own format and log-level behavior.

## Current contents

- `logger.hpp`:
  - `LogLevel` enum (`debug`, `info`, `warn`, `error`)
  - parser for `--log-level`
  - structured line logger that emits:
    - UTC timestamp
    - level
    - run_id
    - message
    - optional key/value fields

## Connection to the project

This logger makes run-time diagnostics traceable to bundle artifacts by
including `run_id` in each line once a run is initialized.
