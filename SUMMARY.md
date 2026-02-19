# LabOps Summary

## Commit: deterministic webcam selector resolution

Date: 2026-02-19

### Goal
Implement deterministic webcam selector resolution for `backend: "webcam"` so runs consistently choose the same webcam by explicit rules (`id`, `index`, `name_contains`, or default index `0`) and record that decision in logs and artifacts.

### Implemented

1. Added a dedicated webcam selector module
- Added `src/backends/webcam/device_selector.hpp` and `src/backends/webcam/device_selector.cpp`.
- New contracts:
  - `WebcamDeviceSelector`
  - `WebcamSelectionRule`
  - `WebcamSelectionResult`
- New behavior:
  - parse webcam selector text (`id:<value>`, `index:<n>`, `name_contains:<text>`)
  - enumerate webcam devices from fixture env var `LABOPS_WEBCAM_DEVICE_FIXTURE`
  - deterministic resolve order:
    1. `id` exact match
    2. `index` in stable-sorted device list
    3. first case-insensitive `name_contains` match
    4. default index `0`

Why:
- Keeps selector behavior explicit and reproducible.
- Gives one reusable place for webcam selection logic instead of embedding it in CLI flow.
- Supports CI/local testing without physical webcams via fixture discovery.

2. Wired webcam selector resolution into run orchestration
- Updated `src/labops/cli/router.cpp`:
  - `RunPlan` now carries optional webcam selector data from `ScenarioModel`.
  - Scenario webcam selector is enforced to backend compatibility (`webcam.device_selector` requires `backend: "webcam"`).
  - Run-time device resolution now supports both real and webcam backends with backend-specific parsing/resolution paths.
  - Selection is logged with rule + selected identity for webcam.
  - Selector metadata is applied to backend params (`device.selector`, `device.selection_rule`, `device.index`, etc.).

Why:
- Ensures deterministic selection happens before backend connect.
- Makes selection choices visible and auditable in structured logs.
- Preserves existing real-backend selector behavior while adding webcam path cleanly.

3. Extended run contract with webcam metadata
- Updated `src/core/schema/run_contract.hpp` and `src/core/schema/run_contract.cpp`:
  - added `WebcamDeviceMetadata`
  - added optional `RunInfo.webcam_device`
  - added JSON serialization for webcam device evidence

Why:
- Captures resolved webcam identity and selector rule directly in `run.json` for triage provenance.
- Prevents “which webcam did this run use?” ambiguity.

4. Exposed selection details in run summary output
- Updated `src/artifacts/run_summary_writer.cpp`:
  - added `## Device Selection` section when real/webcam device metadata is present
  - includes webcam selector text, rule, and index when available

Why:
- Engineers can quickly inspect selected device context from `summary.md` without opening raw JSON.

5. Added schema validation for webcam selector/backend compatibility
- Updated `src/scenarios/validator.cpp`:
  - `webcam.device_selector` now reports actionable error unless backend is `webcam`

Why:
- Stops ambiguous scenario intent early.
- Keeps validate/run behavior aligned for webcam selector semantics.

6. Added focused tests for selector behavior and integration
- Added `tests/backends/webcam_device_selector_smoke.cpp`:
  - parser coverage (`id`, `index`, `name_contains`, invalid keys)
  - deterministic resolution coverage across all rule paths
  - fixture enumeration + stable ordering coverage
- Added `tests/labops/run_webcam_selector_resolution_smoke.cpp`:
  - runs `labops run` with `backend: webcam` + `webcam.device_selector`
  - verifies selector decision is logged
  - verifies `run.json` includes `webcam_device` evidence fields even on connect failure
- Updated existing tests/docs:
  - `tests/schema/run_contract_json_smoke.cpp`
  - `tests/scenarios/scenario_validation_smoke.cpp`
  - `CMakeLists.txt` (new test targets)

Why:
- Locks deterministic selector behavior with regression tests.
- Proves end-to-end evidence contract for webcam selection metadata.

7. Updated docs/readmes for new selector contract
- Updated:
  - `README.md`
  - `docs/scenario_schema.md`
  - `docs/triage_bundle_spec.md`
  - `src/backends/README.md`
  - `src/backends/webcam/README.md`
  - `src/labops/cli/README.md`
  - `src/scenarios/README.md`
  - `tests/backends/README.md`
  - `tests/labops/README.md`
  - `tests/scenarios/README.md`

Why:
- Keeps operator/developer guidance aligned with actual runtime and artifact behavior.
- Prevents contract drift between implementation and docs.

### Files changed
- `CMakeLists.txt`
- `README.md`
- `docs/scenario_schema.md`
- `docs/triage_bundle_spec.md`
- `src/artifacts/run_summary_writer.cpp`
- `src/backends/README.md`
- `src/backends/webcam/README.md`
- `src/backends/webcam/device_selector.hpp`
- `src/backends/webcam/device_selector.cpp`
- `src/core/schema/run_contract.hpp`
- `src/core/schema/run_contract.cpp`
- `src/labops/cli/README.md`
- `src/labops/cli/router.cpp`
- `src/scenarios/README.md`
- `src/scenarios/validator.cpp`
- `tests/backends/README.md`
- `tests/backends/webcam_device_selector_smoke.cpp`
- `tests/labops/README.md`
- `tests/labops/run_webcam_selector_resolution_smoke.cpp`
- `tests/scenarios/README.md`
- `tests/scenarios/scenario_validation_smoke.cpp`
- `tests/schema/run_contract_json_smoke.cpp`
- `SUMMARY.md`

### Verification

1. Formatting
- `bash tools/clang_format.sh --fix`
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- `cmake --build build`
- Result: passed

3. Focused tests
- `ctest --test-dir build -R "webcam_device_selector_smoke|run_webcam_selector_resolution_smoke|scenario_validation_smoke|run_contract_json_smoke" --output-on-failure`
- Result: passed (`4/4`)

4. Full suite
- `ctest --test-dir build --output-on-failure`
- Result: passed (`77/77`)

### Notes
- Deterministic webcam selection is now implemented and test-covered without requiring physical webcams by using fixture-driven discovery.
- `run.json` and `summary.md` now carry webcam selection provenance when resolved.
