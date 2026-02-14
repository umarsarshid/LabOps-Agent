# .github/workflows

GitHub Actions workflows for build, test, and formatting gates.

## Why this folder exists

CI behavior is part of the product contract for this repo. Keeping workflow
definitions versioned with code ensures reproducible build/test quality across
local and remote environments.

## Current workflow

- `ci.yml`
  - runs a formatting gate (`clang-format`)
  - builds and tests on `ubuntu`, `macos`, and `windows`
  - on failure, uploads test triage artifacts so debugging does not require
    rerunning blindly:
    - `out/**`
    - `build/Testing/Temporary/**`

## Connection to the project

When smoke tests fail in CI, uploaded bundles/logs provide the same evidence
shape engineers use locally, which shortens root-cause turnaround.
