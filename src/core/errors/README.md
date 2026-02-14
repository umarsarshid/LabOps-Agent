# src/core/errors

Shared process-exit error contracts used by CLI commands and tests.

## Why this folder exists

Operational tooling and CI need stable, machine-readable exit codes so callers
can distinguish failure categories without parsing free-form stderr text.

## Current contents

- `exit_codes.hpp`:
  - canonical `ExitCode` enum
  - integer conversion helper
  - named categories for usage/schema/backend/threshold outcomes

## Connection to the project

Consistent exit codes make run automation reliable and reduce ambiguity in
pipeline triage.
