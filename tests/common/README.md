# tests/common

Shared test-only helpers used by smoke and integration suites.

## Why this folder exists

Many test files repeated the same boilerplate:
- fail/contains assertions
- temp-directory setup and cleanup
- CLI argv conversion before calling `labops::cli::Dispatch`

Centralizing these keeps tests shorter and makes failure behavior consistent.

## Current contents

- `assertions.hpp`
  - `Fail(...)`
  - `AssertContains(...)`
  - `AssertNotContains(...)`
  - `ReadFileToString(...)`
- `temp_dir.hpp`
  - `CreateUniqueTempDir(...)`
  - `RemovePathBestEffort(...)`
- `cli_dispatch.hpp`
  - `DispatchArgs(...)` for vector-of-strings CLI invocation.

## Connection to the project

Reliable, readable tests are part of LabOps quality. Shared helpers reduce
copy/paste logic so behavior checks are easier to maintain as CLI and artifact
coverage grows.
