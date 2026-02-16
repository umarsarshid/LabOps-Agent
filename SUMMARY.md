# Commit Summary: Add shared test utilities for assertions, temp dirs, and CLI dispatch

Date: 2026-02-16

## Goal
Reduce repeated test boilerplate by introducing a shared `tests/common/` utility layer for:
- text assertions
- temp-directory lifecycle helpers
- CLI dispatch argv conversion

This directly addresses repetition highlighted in:
- `tests/labops/compare_diff_smoke.cpp`
- `tests/agent/agent_state_writer_smoke.cpp`

## Why this change matters
- Many smoke tests redefined nearly identical helpers (`Fail`, `AssertContains`, temp-path creation, argv conversion).
- Repetition increases maintenance cost and creates inconsistent failure messages.
- Shared helpers make tests easier to read and keep test behavior consistent.

## Implementation details

### 1) Added shared test assertion helpers
File added:
- `tests/common/assertions.hpp`

What:
- `Fail(std::string_view)`
- `AssertContains(std::string_view, std::string_view)`
- `AssertNotContains(std::string_view, std::string_view)`
- `ReadFileToString(const std::filesystem::path&)`

Why:
- Standardizes assertion and file-loading behavior across smoke tests.

### 2) Added shared temp-dir helpers
File added:
- `tests/common/temp_dir.hpp`

What:
- `CreateUniqueTempDir(std::string_view prefix)`
- `RemovePathBestEffort(const std::filesystem::path&)`

Why:
- Removes repeated timestamp + temp root + directory create/cleanup code.

### 3) Added shared CLI dispatch helper
File added:
- `tests/common/cli_dispatch.hpp`

What:
- `DispatchArgs(const std::vector<std::string>&)`

Why:
- Removes repeated `std::vector<char*>` argv conversion before calling
  `labops::cli::Dispatch`.

### 4) Added shared helper folder documentation
File added:
- `tests/common/README.md`

What:
- Explains helper purpose and function inventory.

Why:
- Makes onboarding and future reuse straightforward.

### 5) Migrated `compare_diff_smoke` to shared helpers
File changed:
- `tests/labops/compare_diff_smoke.cpp`

What:
- Replaced local `Fail` + `AssertContains` with shared versions.
- Replaced local `DispatchArgs` with shared dispatcher.
- Replaced manual temp-root creation with `CreateUniqueTempDir(...)`.
- Replaced local file read boilerplate with `ReadFileToString(...)`.

Why:
- This file had all three categories of duplicate boilerplate.

### 6) Migrated `agent_state_writer_smoke` to shared helpers
File changed:
- `tests/agent/agent_state_writer_smoke.cpp`

What:
- Replaced local `Fail` + `AssertContains` with shared versions.
- Replaced fixed temp path with unique temp-root helper.
- Replaced local file read boilerplate with `ReadFileToString(...)`.

Why:
- Keeps this test deterministic while removing repeated setup/assertion code.

### 7) Updated test documentation for shared utility usage
Files changed:
- `tests/README.md`
- `tests/labops/README.md`
- `tests/agent/README.md`

What:
- Added references to `tests/common/` and why it should be used.

Why:
- Encourages consistent patterns in future tests.

## Verification

### Formatting
Commands:
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --fix`
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

### Targeted tests
Commands:
- `ctest --test-dir tmp/build -R "compare_diff_smoke|agent_state_writer_smoke|baseline_capture_smoke" --output-on-failure`
- `ctest --test-dir tmp/build-real -R "compare_diff_smoke|agent_state_writer_smoke|baseline_capture_smoke" --output-on-failure`

Result:
- 3/3 passed in each build tree.

## Behavior impact
- No product/runtime behavior changes.
- Test behavior remains equivalent; helper logic is centralized.
- This is a maintainability/refactor commit for test infrastructure.

## Files touched
- `tests/common/assertions.hpp` (new)
- `tests/common/temp_dir.hpp` (new)
- `tests/common/cli_dispatch.hpp` (new)
- `tests/common/README.md` (new)
- `tests/labops/compare_diff_smoke.cpp`
- `tests/agent/agent_state_writer_smoke.cpp`
- `tests/README.md`
- `tests/labops/README.md`
- `tests/agent/README.md`
