# Commit Summary

## Commit
`feat(real): add exposure/gain readback support with range evidence and unit notes`

## What I Implemented

This commit makes `exposure` and `gain` first-class, test-proven real-backend knobs in the OSS real path.

In plain language:
- LabOps now clearly validates/clamps exposure and gain against known safe ranges.
- Readback evidence records what actually got applied after validation.
- `config_report.md` now includes exposure/gain unit + range notes so engineers can read reports without guessing units.

## Why This Was Needed

Real camera triage tickets heavily depend on exposure and gain behavior. Without explicit validation/readback/reporting for these knobs, engineers lose trust in config evidence and waste time confirming units/ranges manually.

This change closes that gap by making exposure/gain behavior deterministic, visible, and verifiable in both backend and CLI artifact tests.

## Detailed Changes

### 1) Strengthened range-adjustment evidence in real param apply
File:
- `src/backends/real_sdk/apply_params.cpp`

Changes:
- Added numeric range text formatting helper.
- `ClampWithRange(...)` now records richer adjustment reason:
  - from: `clamped from X to Y`
  - to: `clamped from X to Y (allowed range [min, max])`

Why:
- range validation already existed, but evidence text now explicitly carries the allowed range, which is more useful for triage and report readability.

### 2) Added unit/range notes for exposure and gain in config report
File:
- `src/artifacts/config_report_writer.cpp`

Changes:
- Added key-specific note helper for:
  - `exposure` -> `units: us; validated range: [5, 10000000]`
  - `gain` -> `units: dB; validated range: [0, 48]`
- Notes are appended per-row in `BuildReportRows(...)` so they appear in the report table regardless of status.

Why:
- engineers reviewing `config_report.md` should immediately see unit context and expected bounds for these two high-impact knobs.

### 3) Expanded backend smoke coverage for exposure/gain apply + readback
File:
- `tests/backends/real_apply_params_smoke.cpp`

Changes:
- Best-effort scenario now includes:
  - `exposure = 20000000` (above max)
  - `gain = -2` (below min)
- Added assertions that:
  - both are marked `adjusted`
  - applied values clamp correctly (`10000000`, `0`)
  - readback rows report clamped actual values
  - backend receives mapped SDK nodes (`ExposureTime`, `Gain`)

Why:
- this proves done-condition behavior at the adapter/apply layer independent of build-time real backend enablement.

### 4) Expanded report-writer smoke to verify exposure/gain unit notes
File:
- `tests/artifacts/config_report_writer_smoke.cpp`

Changes:
- Added readback rows for exposure and gain.
- Updated expected status counters (`adjusted` count increased).
- Added assertions for unit/range note text in markdown.

Why:
- guards the new report contract and prevents future regression on unit note visibility.

### 5) Expanded CLI integration smoke for exposure/gain evidence capture
File:
- `tests/labops/run_device_selector_resolution_smoke.cpp`

Changes:
- Real-run scenario now requests:
  - `exposure_us: 8000`
  - `gain_db: 6.5`
- Added assertions (real-enabled path) that artifacts include exposure/gain evidence:
  - `config_verify.json` has `generic_key: exposure/gain` and expected actual values.
  - `config_report.md` includes exposure/gain rows and unit/range notes.

Why:
- validates end-to-end recording in CLI artifact flow, not just unit-level apply logic.

### 6) Documentation/readme updates for new knob behavior
Files:
- `src/backends/real_sdk/README.md`
- `src/artifacts/README.md`
- `tests/backends/README.md`
- `tests/labops/README.md`
- `docs/triage_bundle_spec.md`

Changes:
- documented exposure/gain range constraints and report unit-note behavior.
- updated test-folder docs to reflect new coverage.
- bundle spec now explicitly states exposure/gain unit/range hints in `config_report.md`.

Why:
- keeps docs aligned with behavior and reduces onboarding ambiguity for future engineers.

### 7) Incidental formatting-only adjustment
File:
- `tests/labops/run_interrupt_flush_smoke.cpp`

Change:
- include order updated by `clang-format` during repo-wide format pass.

Why:
- required to keep formatting checks clean.

## Verification Performed

### Format
- `bash tools/clang_format.sh --check` (initial failure)
- `bash tools/clang_format.sh --fix`
- `bash tools/clang_format.sh --check`
- Result: pass

### Build
- `cmake --build build`
- Result: pass

### Focused tests
- `ctest --test-dir build -R "real_apply_params_smoke|config_report_writer_smoke|run_device_selector_resolution_smoke|real_apply_mode_events_smoke" --output-on-failure`
- Result: pass (`4/4`)

### Full suite
- `ctest --test-dir build --output-on-failure`
- Result: pass (`59/59`)

## Risk Assessment

Low:
- additive evidence/reporting behavior
- no breaking removal of existing fields/contracts
- covered by backend unit-smoke + CLI integration smoke + full suite pass

## Outcome

Exposure and gain are now minimum-useful real knobs with:
- deterministic range validation/clamping evidence,
- explicit readback recording,
- and human-readable unit/range notes in config reports.

This makes real-ticket triage around exposure/gain much faster and less error-prone.
