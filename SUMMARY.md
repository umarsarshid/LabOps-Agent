# Refactor Audit Follow-up — Shared Device Selector Validation

Date: 2026-02-16

## Commit Goal
Implement the selected refactor (#4): make scenario validation and runtime use the same device selector parser so they cannot drift.

## What Was Implemented

### 1) Scenario validator now reuses runtime selector parser

File:
- `src/scenarios/validator.cpp`

Change:
- Added include for runtime selector API:
  - `#include "backends/real_sdk/real_backend_factory.hpp"`
- Replaced the in-file hand-rolled `device_selector` parsing logic with a call to:
  - `backends::real_sdk::ParseDeviceSelector(...)`
- Kept backend compatibility validation in validator:
  - `device_selector` still requires `backend == "real_stub"`

Why:
- Before this change, there were two independent parsers (one in validator, one in runtime path).
- That creates a real drift risk: `labops validate` could pass while `labops run` rejects (or vice versa).
- Using one parser eliminates split-brain selector behavior.

Behavior impact:
- Selector syntax/rules now come from the same parser for both validate and run paths.
- Validation backend requirement remains enforced exactly as before.

---

### 2) Error wording alignment for compatibility

File:
- `src/backends/real_sdk/real_backend_factory.cpp`

Change:
- Updated empty-selector-value error text from:
  - `is missing a value`
- To:
  - `must provide a non-empty value (missing a value)`

Why:
- Needed to satisfy both existing expectations:
  - validator smoke checks for `non-empty value`
  - backend selector smoke checks for `missing a value`
- This preserves compatibility while still being clearer.

---

### 3) Build linkage updated for shared parser usage

File:
- `CMakeLists.txt`

Change:
- Linked scenarios module against backends module:
  - `target_link_libraries(labops_scenarios PUBLIC labops_backends)`

Why:
- `validator.cpp` now calls `ParseDeviceSelector(...)` implemented in backends.
- Without this link, scenarios library would not resolve the symbol.

Notes:
- Build succeeds.
- Linker reports duplicate static-lib warnings on some test/link targets (`liblabops_backends.a`), but behavior remains correct and tests pass.

## Verification

### Formatting
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --fix`
- `CLANG_FORMAT_REQUIRED_MAJOR=21 bash tools/clang_format.sh --check`
- Result: pass

### Configure + Build
- `cmake -S . -B tmp/build`
- `cmake -S . -B tmp/build-real -DLABOPS_ENABLE_REAL_BACKEND=ON`
- `cmake --build tmp/build`
- `cmake --build tmp/build-real`
- Result: pass

### Targeted tests (selector + validator focus)
- `ctest --test-dir tmp/build --output-on-failure -R "scenario_validation_smoke|run_device_selector_resolution_smoke|real_device_selector_resolution_smoke|list_devices_real_backend_smoke|real_apply_mode_events_smoke|validate_actionable_smoke"`
- `ctest --test-dir tmp/build-real --output-on-failure -R "scenario_validation_smoke|run_device_selector_resolution_smoke|real_device_selector_resolution_smoke|list_devices_real_backend_smoke|real_apply_mode_events_smoke|validate_actionable_smoke"`
- Result: all pass on both build trees

## Risk Assessment
- Scope is narrow and intentional.
- Main risk (behavior drift between validation and runtime) is reduced.
- Existing selector/validator smokes confirm contract stability.
