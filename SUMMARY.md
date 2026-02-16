# Commit Summary: Deduplicate JSON/UTC/output-dir utility helpers

Date: 2026-02-16

## Goal
Remove repeated helper implementations for:
- JSON string escaping
- UTC timestamp formatting
- output directory creation for artifact writers

This refactor targets the duplicated logic currently seen in:
- `src/artifacts/config_report_writer.cpp`
- `src/artifacts/config_verify_writer.cpp`
- `src/core/schema/run_contract.cpp`
- `src/events/event_model.cpp`

## Why this change matters
- The same helper logic had been copy/pasted across modules.
- Duplicate helpers are easy to accidentally diverge over time.
- Divergence in JSON escaping or timestamp formatting would create subtle, hard-to-debug inconsistencies in artifacts/events.
- Shared output-dir creation logic keeps failure text and behavior consistent across artifact writers and tests.

## Implementation details

### 1) Added shared core JSON helper
File added:
- `src/core/json_utils.hpp`

What:
- Added `core::EscapeJson(std::string_view)` as a shared, inline utility.

Why:
- Provides one canonical JSON escaping implementation for schema, events, and artifact writers.

### 2) Added shared core UTC timestamp helper
File added:
- `src/core/time_utils.hpp`

What:
- Added `core::FormatUtcTimestamp(std::chrono::system_clock::time_point)`.
- Preserves existing millisecond-precision UTC output contract.

Why:
- Ensures all timestamped outputs use the exact same formatter and platform handling.

### 3) Added shared artifact output-dir helper
File added:
- `src/artifacts/output_dir_utils.hpp`

What:
- Added `artifacts::EnsureOutputDir(const std::filesystem::path&, std::string&)`.
- Keeps existing behavior and error-message wording.

Why:
- Avoids repeated filesystem guard code in every artifact writer.

### 4) Rewired `run_contract` to shared core helpers
File changed:
- `src/core/schema/run_contract.cpp`

What:
- Removed local `EscapeJson` and `FormatUtcTimestamp` implementations.
- Switched serialization calls to `core::EscapeJson(...)` and `core::FormatUtcTimestamp(...)`.

Why:
- Run contract JSON now depends on shared serialization/time utility code rather than local copies.

### 5) Rewired event model to shared core helpers
File changed:
- `src/events/event_model.cpp`

What:
- Removed local `EscapeJson` and `FormatUtcTimestamp` implementations.
- Switched event serialization calls to `core::EscapeJson(...)` and `core::FormatUtcTimestamp(...)`.

Why:
- Event JSONL output now uses the exact same escape/time implementation as run contracts.

### 6) Rewired config verify writer to shared helpers
File changed:
- `src/artifacts/config_verify_writer.cpp`

What:
- Removed local `EscapeJson` and local output-dir helper.
- Switched to `core::EscapeJson(...)` and `artifacts::EnsureOutputDir(...)`.

Why:
- Eliminates duplicated JSON and filesystem utility logic in this artifact writer.

### 7) Rewired config report writer to shared helpers
File changed:
- `src/artifacts/config_report_writer.cpp`

What:
- Removed local UTC formatter and local output-dir helper.
- Switched to `core::FormatUtcTimestamp(...)` and `artifacts::EnsureOutputDir(...)`.

Why:
- Keeps timestamp rendering and directory creation behavior aligned with all other modules.

### 8) Updated module docs for discoverability
Files changed:
- `src/core/README.md`
- `src/artifacts/README.md`

What:
- Documented the new shared helper headers and their purpose.

Why:
- Helps future engineers find the single source of truth quickly.

### 9) Formatting normalization discovered during check
Files changed:
- `src/scenarios/netem_profile_support.cpp`
- `src/scenarios/netem_profile_support.hpp`

What:
- `clang-format`-only line wrapping changes.

Why:
- Required to keep CI formatter checks passing with clang-format v21.

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

### Targeted regression tests
Commands:
- `ctest --test-dir tmp/build -R "run_contract_json_smoke|events_jsonl_smoke|config_verify_writer_smoke|config_report_writer_smoke" --output-on-failure`
- `ctest --test-dir tmp/build-real -R "run_contract_json_smoke|events_jsonl_smoke|config_verify_writer_smoke|config_report_writer_smoke" --output-on-failure`

Result:
- 4/4 passed in each build tree.

## Behavior impact
- Intended behavior is unchanged.
- This is a consistency/maintainability refactor that reduces helper duplication and drift risk.
- Serialization contracts and timestamp format remain the same.

## Files touched
- `src/core/json_utils.hpp` (new)
- `src/core/time_utils.hpp` (new)
- `src/artifacts/output_dir_utils.hpp` (new)
- `src/core/schema/run_contract.cpp`
- `src/events/event_model.cpp`
- `src/artifacts/config_verify_writer.cpp`
- `src/artifacts/config_report_writer.cpp`
- `src/core/README.md`
- `src/artifacts/README.md`
- `src/scenarios/netem_profile_support.cpp` (format-only)
- `src/scenarios/netem_profile_support.hpp` (format-only)
