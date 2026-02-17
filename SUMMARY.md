# Commit Summary

## Commit
`feat(metrics): classify timeouts vs incomplete vs drops`

## What I Implemented

This commit splits dropped-frame metrics into three explicit categories so engineers can tell exactly what kind of failure happened:
- generic drops
- acquisition timeouts
- incomplete frames

The split is now reflected consistently in:
- in-memory metrics (`FpsReport`)
- persisted artifacts (`metrics.csv`, `metrics.json`)
- human summaries (`summary.md`, `report.html`)
- anomaly text
- CLI run output
- tests and bundle spec docs

## Why This Was Needed

Before this change, all non-received frames were effectively one "drop" bucket. That hid useful triage signal:
- timeout-heavy runs usually indicate acquisition/transport wait problems
- incomplete-heavy runs usually indicate payload integrity issues
- generic drops can reflect simulated fault injection or other non-timeout loss

Separating these counters makes triage faster and reduces guesswork.

## Detailed Changes

### 1) Extended metrics contract with explicit category counters/rates
Files:
- `src/metrics/fps.hpp`
- `src/metrics/fps.cpp`

Changes:
- Added new `FpsReport` totals:
  - `dropped_generic_frames_total`
  - `timeout_frames_total`
  - `incomplete_frames_total`
- Added new `FpsReport` rates:
  - `generic_drop_rate_percent`
  - `timeout_rate_percent`
  - `incomplete_rate_percent`
- Updated computation logic to classify frames by `FrameOutcome`:
  - `kTimeout` -> timeout bucket + total dropped
  - `kIncomplete` -> incomplete bucket + total dropped
  - `kDropped` -> generic dropped bucket + total dropped
- Added backward-compatible fallback:
  - if legacy sample has `outcome=kReceived` but `dropped=true`, classify as generic dropped.

Why:
- keeps old fixtures/runs working while introducing richer classification for new data.

### 2) Persisted new categories in machine and spreadsheet artifacts
File:
- `src/artifacts/metrics_writer.cpp`

Changes:
- `metrics.csv` now includes:
  - `drops_generic_total`
  - `timeouts_total`
  - `incomplete_total`
  - `generic_drop_rate_percent`
  - `timeout_rate_percent`
  - `incomplete_rate_percent`
- `metrics.json` now includes matching total/rate fields.

Why:
- agent and automation need machine-readable category metrics.
- engineers exporting CSV need category visibility for quick plots.

### 3) Exposed category metrics in human-facing summaries
Files:
- `src/artifacts/run_summary_writer.cpp`
- `src/artifacts/html_report_writer.cpp`

Changes:
- Added category totals/rates to key metric sections.
- Added category rate rows to HTML diff table.

Why:
- one-page summaries should show root-signal metrics without requiring raw JSON inspection.

### 4) Updated anomaly and CLI quick output to include breakdown
Files:
- `src/metrics/anomalies.cpp`
- `src/labops/cli/router.cpp`

Changes:
- Drop anomaly now prints:
  - total drop count/rate plus
  - generic/timeout/incomplete breakdown.
- run console line (`drops:`) now includes all three category counts.

Why:
- improves immediate triage signal during live runs and in summary text.

### 5) Kept metric compare output order aligned
File:
- `src/artifacts/metrics_diff_writer.cpp`

Changes:
- Added new category totals/rates to preferred metric ordering.

Why:
- keeps diffs stable and readable when comparing baseline vs run.

### 6) Updated module and contract docs
Files:
- `src/metrics/README.md`
- `docs/triage_bundle_spec.md`

Changes:
- documented the new category split in metrics module README.
- expanded bundle spec metric contract for CSV/JSON fields and formulas.

Why:
- prevents drift between implementation and expected artifact schema.

### 7) Expanded test coverage for category separation
Files:
- `tests/metrics/fps_metrics_smoke.cpp`
- `tests/metrics/drop_injection_smoke.cpp`
- `tests/backends/real_frame_acquisition_smoke.cpp`
- `tests/artifacts/metrics_writers_smoke.cpp`
- `tests/artifacts/html_report_writer_smoke.cpp`
- `tests/scenarios/sim_baseline_metrics_integration_smoke.cpp`

Coverage added:
- mixed-outcome fixture validates total + per-category counters/rates.
- sim drop injection validates generic bucket only.
- real acquisition smoke validates timeout/incomplete buckets map correctly.
- artifact writers include all new fields.
- baseline integration confirms category fields exist and are zero for clean run.

Why:
- ensures category split is correct in compute path, serialization path, and integration path.

## Verification Performed

### Format
- `bash tools/clang_format.sh --check` (initially failed)
- `bash tools/clang_format.sh --fix`
- `bash tools/clang_format.sh --check`
- Result: pass

### Build
- `cmake --build build`
- Result: pass

### Tests
- `ctest --test-dir build --output-on-failure`
- Result: pass (`58/58`)

## Risk Assessment

Low:
- additive metric fields and derived formulas
- no removal of existing fields (`dropped_frames_total`, `drop_rate_percent` remain)
- legacy compatibility fallback included for older frame samples
- full smoke suite passed

## Outcome

LabOps now reports dropped frame causes separately (generic vs timeout vs incomplete) across compute, artifact, and summary layers, so engineers can triage failures with much better signal and less ambiguity.
