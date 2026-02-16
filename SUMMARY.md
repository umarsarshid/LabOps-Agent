# Commit Summary: Unify netem profile path resolution + slug rules

Date: 2026-02-16

## Goal
Remove duplicated netem helper logic that existed in both the CLI run path and scenario validation path, so both paths resolve profile IDs and enforce slug rules the same way.

## Why this change
- The same logic was implemented in two places:
  - `src/labops/cli/router.cpp`
  - `src/scenarios/validator.cpp`
- Duplication creates drift risk: one path can silently change while the other path stays old.
- Netem behavior should be contract-consistent between `labops validate` and `labops run`.

## Implementation

### 1. Added a shared scenarios helper module
Files added:
- `src/scenarios/netem_profile_support.hpp`
- `src/scenarios/netem_profile_support.cpp`

What was added:
- `bool IsLowercaseSlug(std::string_view value)`
- `bool ResolveNetemProfilePath(const std::filesystem::path& scenario_path, std::string_view profile_id, std::filesystem::path& resolved_path)`

Why:
- Centralizes slug rule and profile-path resolution into one place used by all call sites.
- Makes future changes one-edit instead of two-edit.

### 2. Wired helper into build target
File changed:
- `CMakeLists.txt`

What changed:
- Added `src/scenarios/netem_profile_support.cpp` to the scenarios library target.

Why:
- Ensures the shared helper is compiled once and linked wherever scenarios functionality is used.

### 3. Replaced local duplicate logic in CLI router
File changed:
- `src/labops/cli/router.cpp`

What changed:
- Included `scenarios/netem_profile_support.hpp`.
- Removed local static slug/path helper implementations.
- Updated call sites to use `scenarios::IsLowercaseSlug(...)` and `scenarios::ResolveNetemProfilePath(...)`.

Why:
- CLI run flow now relies on the same rules as validator flow.

### 4. Replaced local duplicate logic in scenario validator
File changed:
- `src/scenarios/validator.cpp`

What changed:
- Included `scenarios/netem_profile_support.hpp`.
- Removed local static slug/path helper implementations.
- Updated validator call sites to use shared helper functions.

Why:
- Validation flow now shares exactly the same source of truth with run flow.

### 5. Updated module documentation
File changed:
- `src/scenarios/README.md`

What changed:
- Added explanation for `netem_profile_support` and how it connects validate/run behavior.

Why:
- Helps future engineers quickly find the canonical netem helper location.

## Verification

### Formatting
Command:
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check`

Result:
- Passed.

### Configure
Commands:
- `cmake -S . -B tmp/build`

Result:
- Passed.

### Build
Command:
- `cmake --build tmp/build`

Result:
- Passed.

### Focused tests for impacted behavior
Command:
- `ctest --test-dir tmp/build -R "run_stream_trace_smoke|validate_actionable_smoke|netem_option_contract_smoke|scenario_validation_smoke" --output-on-failure`

Result:
- 4/4 tests passed.

## Behavior impact
- No intended functional behavior change.
- This is a maintenance/refactor improvement:
  - same rules
  - one shared implementation
  - lower risk of future drift.

## Commit contents
- `CMakeLists.txt`
- `src/labops/cli/router.cpp`
- `src/scenarios/README.md`
- `src/scenarios/validator.cpp`
- `src/scenarios/netem_profile_support.cpp`
- `src/scenarios/netem_profile_support.hpp`
- `SUMMARY.md`
