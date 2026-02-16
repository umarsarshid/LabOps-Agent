# 0077 — Real Config Markdown Report (`config_report.md`)

Date: 2026-02-16

## Goal
Add a human-readable config report artifact for real-backend runs:
- `config_report.md`

Required status table semantics:
- ✅ applied
- ⚠ adjusted (constraints)
- ❌ unsupported

Done-when target:
- engineers can read config outcomes without opening JSON artifacts.

## Why this change
After 0076, bundles had strong JSON evidence (`config_verify.json`,
`camera_config.json`) but triage still required opening JSON for quick read.

This commit adds a markdown summary optimized for first-pass review.
It keeps the same source-of-truth evidence but presents it in a table
engineers can scan immediately.

## Implementation

### 1) New artifact writer: `config_report_writer`
Added:
- `src/artifacts/config_report_writer.hpp`
- `src/artifacts/config_report_writer.cpp`

Writer contract:
- emits `<bundle>/config_report.md`
- includes run metadata (`run_id`, `scenario_id`, `backend`, `apply_mode`)
- includes optional collection-error notes
- includes status summary counts
- includes status table with columns:
  - `Status`, `Key`, `Node`, `Requested`, `Actual`, `Notes`
- uses explicit status mapping:
  - `✅ applied` when supported+applied and not adjusted
  - `⚠ adjusted` when supported+applied+adjusted
  - `❌ unsupported` otherwise

Engineering details:
- markdown cell escaping for `|` and multiline text in reasons
- deterministic row ordering by key/node for stable diffs
- empty fields normalized to `-`

Why:
- gives triage teams a one-page config outcome view.
- preserves deterministic formatting for CI snapshots and review diffs.

### 2) Run pipeline wiring
Updated:
- `src/labops/cli/router.cpp`

Changes:
- `ApplyRealParamsWithEvents(...)` now tracks/writes three config artifacts:
  - `config_verify.json`
  - `camera_config.json`
  - `config_report.md`
- writes `config_report.md` on both success and failure paths of real apply.
- for pre-apply setup failures (key-map load/adapter init), still attempts to
  write human-readable config artifacts with collection-error context.
- surfaces artifact path in connect-failure output.
- includes `config_report.md` in bundle manifest lists (soak pause + final run).
- includes `config_report` in logger artifact summary and stdout paths.

Why:
- keeps config report available even when run exits before stream start.
- makes artifact discoverability consistent with existing outputs.

### 3) Build/test integration
Updated:
- `CMakeLists.txt`

Changes:
- added `src/artifacts/config_report_writer.cpp` to `labops_artifacts`
- added `config_report_writer_smoke` test target

Why:
- ensures writer compiles in both default and real-enabled build trees.
- ensures regression protection for output contract.

### 4) Tests
Added:
- `tests/artifacts/config_report_writer_smoke.cpp`

Updated:
- `tests/labops/real_apply_mode_events_smoke.cpp`
  - now asserts `config_report.md` exists and contains status table rows
    (`✅`, `⚠`, `❌`) where expected.
- `tests/labops/run_device_selector_resolution_smoke.cpp`
  - now asserts `config_report.md` exists and includes readable status table
    content in real-backend path.
- `tests/artifacts/camera_config_writer_smoke.cpp`
  - formatting-only normalization from shared clang-format pass.
- `src/artifacts/camera_config_writer.cpp`
  - formatting-only normalization from shared clang-format pass.

Why:
- validates the new artifact in both isolated writer tests and full CLI flow.

### 5) Documentation/readme updates
Updated:
- `README.md`
- `docs/triage_bundle_spec.md`
- `src/artifacts/README.md`
- `src/labops/README.md`
- `src/labops/cli/README.md`
- `src/backends/README.md`
- `src/backends/real_sdk/README.md`
- `tests/artifacts/README.md`
- `tests/backends/README.md`
- `tests/labops/README.md`

Documentation changes include:
- bundle layout now lists `config_report.md` for real-backend runs
- lifecycle sequence includes config report generation
- artifact table includes `config_report.md`
- file-contract section defines report purpose and table fields

Why:
- keeps implementation and docs aligned for handoff and onboarding.

## Verification

### Formatting
Commands:
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --fix`
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check`

Result:
- check passed

### Build
Commands:
- `cmake --build tmp/build`
- `cmake --build tmp/build-real`

Result:
- both passed

### Targeted tests (real build tree)
Command:
- `ctest --test-dir tmp/build-real --output-on-failure -R "config_report_writer_smoke|camera_config_writer_smoke|config_verify_writer_smoke|real_apply_mode_events_smoke|run_device_selector_resolution_smoke"`

Result:
- all passed (5/5)

### Targeted tests (default build tree)
Command:
- `ctest --test-dir tmp/build --output-on-failure -R "config_report_writer_smoke|camera_config_writer_smoke|config_verify_writer_smoke|run_stream_trace_smoke"`

Result:
- all passed (4/4)

### Manual proof
Commands:
- created `tmp/verify-0077/scenario_real_config_report.json`
- ran:
  - `./tmp/build-real/labops run tmp/verify-0077/scenario_real_config_report.json --out tmp/runs-0077`
- inspected:
  - `<run>/config_report.md`

Observed:
- run exits with backend-connect-failed (expected in current real-disabled path)
- bundle includes:
  - `config_verify.json`
  - `camera_config.json`
  - `config_report.md`
- `config_report.md` contains readable table with:
  - `⚠ adjusted` row (frame_rate clamp)
  - `✅ applied` rows (gain/pixel_format)
  - `❌ unsupported` row (trigger_mode)

## Outcome
0077 objective met:
- real-backend runs now produce a markdown config status report.
- engineers can read applied/adjusted/unsupported outcomes directly from
  `config_report.md` without opening JSON files.
