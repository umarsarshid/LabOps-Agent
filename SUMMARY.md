# Commit Summary: Refactor real apply-params unsupported handling (strict vs best-effort)

Date: 2026-02-16

## Goal
Reduce repetitive error-handling branches in real parameter apply flow by centralizing unsupported-setting handling into one helper-driven path.

Target:
- `src/backends/real_sdk/apply_params.cpp`

## Why this change matters
- `ApplyParams(...)` had many near-identical branches for:
  - map failure
  - missing node
  - parse failures (bool/int/float)
  - node write failures (bool/int/float/string)
  - unknown node type
- Each branch repeated the same sequence:
  1. create unsupported reason
  2. fill readback row
  3. push `result.readback_rows`
  4. push `result.unsupported`
  5. strict-mode fail vs best-effort continue
- Repetition increases drift risk (small inconsistencies in supported/applied/reason/error behavior).

## Implementation details

### 1) Added centralized helper for unsupported handling
File changed:
- `src/backends/real_sdk/apply_params.cpp`

What:
- Added internal helper:
  - `RecordUnsupportedParameter(...)`

Helper responsibilities:
- build and append a canonical `ReadbackRow` for unsupported outcomes
- append `UnsupportedParam` entry
- enforce strict mode behavior (`error` + return false)
- allow best-effort behavior (return true, caller continues)

Why:
- One source of truth for strict vs best-effort handling and unsupported evidence emission.

### 2) Rewired `ApplyParams(...)` to use helper
File changed:
- `src/backends/real_sdk/apply_params.cpp`

What:
- Replaced repeated unsupported branches with `RecordUnsupportedParameter(...)` calls for:
  - generic key mapping missing
  - mapped node unavailable
  - bool/int/float parse failures
  - bool/int/float/string node write rejections
  - unknown node type
- Introduced `resolved_node_name` optional once per resolved key and reused it in all helper calls.
- Introduced `node_type` local to avoid repeated `GetType(...)` calls and keep switch/enum checks consistent.

Why:
- Keeps logic behaviorally equivalent while removing repeated branch code.
- Makes future policy changes (strict/best-effort behavior) safer and easier.

### 3) Preserved existing behavior contracts
Behavior intentionally preserved:
- strict mode fails on first unsupported condition with same error style:
  - `unsupported parameter '<key>': <reason>`
- best-effort mode records unsupported rows and continues.
- supported/applied flags remain aligned to previous semantics:
  - parse/write failures: `supported=true`, `applied=false`
  - mapping missing / node missing / unknown type: `supported=false`, `applied=false`
- backend `SetParam(...)` failure path remains hard-fail (not treated as unsupported).

### 4) Formatting normalization
File changed:
- `tests/common/temp_dir.hpp`

What:
- `clang-format`-only line-wrap normalization discovered by formatter check.

Why:
- Keeps repository-wide formatter checks green.

## Verification

### Formatting
Commands:
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check`

Result:
- Passed.

### Configure
Commands:
- `cmake -S . -B tmp/build`
- `cmake -S . -B tmp/build-real -DLABOPS_ENABLE_REAL_BACKEND=ON`

Result:
- Both passed.

### Build
Commands:
- `cmake --build tmp/build`
- `cmake --build tmp/build-real`

Result:
- Both passed.

### Focused regression tests (real apply flow)
Commands:
- `ctest --test-dir tmp/build -R "real_apply_params_smoke|real_apply_mode_events_smoke|real_device_selector_resolution_smoke|run_device_selector_resolution_smoke" --output-on-failure`
- `ctest --test-dir tmp/build-real -R "real_apply_params_smoke|real_apply_mode_events_smoke|real_device_selector_resolution_smoke|run_device_selector_resolution_smoke" --output-on-failure`

Result:
- 4/4 passed in each build tree.

## Behavior impact
- Intended runtime behavior unchanged.
- Primary impact is maintainability and consistency of unsupported-handling paths.

## Files touched
- `src/backends/real_sdk/apply_params.cpp`
- `tests/common/temp_dir.hpp` (format-only)

