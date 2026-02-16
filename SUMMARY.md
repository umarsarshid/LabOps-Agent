# Commit Summary: Add reusable CMake smoke-test macro

Date: 2026-02-16

## Goal
Reduce CMake maintenance overhead by replacing repeated smoke-test declaration boilerplate (`add_executable` + `target_link_libraries` + `target_compile_features` + `add_test`) with one shared helper macro.

## Why this change matters
- The smoke suite had dozens of near-identical CMake blocks.
- Repetition made updates risky and time-consuming (easy to miss one test when changing compile/link policy).
- Centralizing the pattern ensures all smoke tests get consistent compile features and test registration.

## Implementation details

### 1) Added a small reusable macro
File changed:
- `CMakeLists.txt`

What:
- Added `add_labops_smoke_test(...)` macro with arguments:
  - `NAME`
  - `SOURCES`
  - optional `LIBRARIES`
  - optional `INCLUDE_DIRS`
- Macro behavior:
  - validates required args (`NAME`, `SOURCES`)
  - creates executable
  - applies include directories (if present)
  - links libraries (if present)
  - enforces `cxx_std_20`
  - registers test via `add_test(NAME ... COMMAND ...)`

Why:
- Captures the repeated pattern in one place.
- Prevents future drift in compile/test setup across smoke targets.

### 2) Added shared CLI smoke-library list
File changed:
- `CMakeLists.txt`

What:
- Added `LABOPS_CLI_SMOKE_LIBRARIES` list variable containing:
  - `labops_artifacts`
  - `labops_backends`
  - `labops_events`
  - `labops_metrics`
  - `labops_scenarios`
  - `labops_schema`

Why:
- Many CLI integration smoke tests shared the exact same library set.
- Reduces repeated long link lines and improves readability.

### 3) Migrated smoke test targets to macro usage
File changed:
- `CMakeLists.txt`

What:
- Replaced all repeated smoke-target blocks with `add_labops_smoke_test(...)` calls.
- Preserved comments and logical grouping.
- Preserved special cases:
  - tests needing `src/labops/cli/router.cpp` include that in `SOURCES`
  - tests needing `src` include path use `INCLUDE_DIRS src`
- Left non-smoke targets unchanged:
  - `labops` executable
  - `core_unit_tests` Catch2 block

Why:
- Full adoption gives immediate maintenance benefit.
- Keeps behavior equivalent while removing repetitive setup code.

## Verification

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
- All smoke executables still build successfully in both trees.

### Targeted smoke runtime checks
Commands:
- `ctest --test-dir tmp/build -R "run_contract_json_smoke|events_jsonl_smoke|run_stream_trace_smoke|compare_diff_smoke|agent_state_writer_smoke|scenario_validation_smoke|list_backends_smoke|netem_option_contract_smoke" --output-on-failure`
- `ctest --test-dir tmp/build-real -R "run_contract_json_smoke|events_jsonl_smoke|run_stream_trace_smoke|compare_diff_smoke|agent_state_writer_smoke|scenario_validation_smoke|list_backends_smoke|netem_option_contract_smoke" --output-on-failure`

Result:
- 8/8 passed in each build tree.

## Behavior impact
- No product/runtime logic changes.
- Build/test behavior is intended to be equivalent.
- Primary impact is build-definition maintainability and consistency.

## Files touched
- `CMakeLists.txt`

