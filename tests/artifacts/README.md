# tests/artifacts

Artifact-focused smoke/regression tests.

## Why this folder exists

Artifact contracts are consumed by humans, CI, and future automation. Tests in
this folder guard against accidental output-shape or file-path regressions.

## Current contents

- `run_writer_smoke.cpp`: verifies `WriteRunJson(...)` creates `run.json` in
  the requested output directory and emits required metadata fields.
- `metrics_writers_smoke.cpp`: verifies `WriteMetricsCsv(...)` and
  `WriteMetricsJson(...)` create both metrics artifacts with expected fields.
- `scenario_writer_smoke.cpp`: verifies `WriteScenarioJson(...)` copies a
  source scenario file into `<bundle>/scenario.json`.
- `bundle_manifest_writer_smoke.cpp`: verifies
  `WriteBundleManifestJson(...)` emits `bundle_manifest.json` including each
  artifact path with hash and size fields.
- `bundle_zip_writer_smoke.cpp`: verifies `WriteBundleZip(...)` emits a valid
  zip file (`PK` signature) and includes expected bundle entries.

## Connection to the project

Reliable artifact generation is a core promise of LabOps. These tests ensure
every run continues to produce reproducible evidence files.
