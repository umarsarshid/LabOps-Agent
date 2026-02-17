# Commit Summary

## Commit
`refactor(core): unify shared JSON parser for scenarios and agent`

## What I Implemented

I removed duplicated JSON parser implementations from:
- `src/scenarios/validator.cpp`
- `src/agent/variant_generator.cpp`

and replaced both with one shared parser in:
- `src/core/json_dom.hpp`

## Why This Matters

Before this change, both modules had their own full parser copy. That caused two maintenance problems:
1. Bug fixes had to be duplicated.
2. Parser behavior could drift subtly between scenario validation and agent variant generation.

Now both modules parse JSON with the same code path, so parser fixes and features are applied once.

## File-by-File Changes

### 1) Added shared parser module
- **Added**: `src/core/json_dom.hpp`

What it contains:
- `labops::core::json::Value` (shared lightweight JSON DOM)
- `labops::core::json::Parser` (shared parser implementation)
- `Parse(...)` helper wrapper

Parser behavior kept aligned with previous validator parser quality:
- supports object/array/string/number/bool/null
- same basic escape behavior
- explicit unsupported unicode escape error (`\\uXXXX`)
- line/column parse diagnostics (`parse error at line X, col Y: ...`)

Why:
- one parser implementation across modules
- no extra external dependency
- header-only integration kept build changes minimal

### 2) Rewired scenario validator to shared parser
- **Updated**: `src/scenarios/validator.cpp`

Changes:
- Added include: `core/json_dom.hpp`
- Removed local duplicated `JsonValue` + `JsonParser` implementation
- Added aliases:
  - `using JsonValue = core::json::Value;`
  - `using JsonParser = core::json::Parser;`

Why:
- scenario validation now uses the shared parser while keeping existing validator logic and issue reporting flow unchanged.

### 3) Rewired OAAT variant generator to shared parser
- **Updated**: `src/agent/variant_generator.cpp`

Changes:
- Added include: `core/json_dom.hpp`
- Removed local duplicated `JsonValue` + `JsonParser` implementation
- Added aliases:
  - `using JsonValue = core::json::Value;`
  - `using JsonParser = core::json::Parser;`

Why:
- agent variant generation now parses with the same implementation as scenario validation, eliminating duplication and divergence.

### 4) Updated core module docs
- **Updated**: `src/core/README.md`

Added `json_dom.hpp` in current contents section.

Why:
- keeps module-level documentation accurate for future engineers.

## Verification Performed

### Formatting
- `bash tools/clang_format.sh --check`
- Result: **pass**.

### Build
- `cmake --build build`
- Result: **pass**.

### Targeted tests for changed behavior
- `ctest --test-dir build -R "(scenario_validation_smoke|oaat_variant_generator_smoke|agent_triage_integration_smoke)" --output-on-failure`
- Result: **3/3 passed**.

### Full regression run
- `ctest --test-dir build --output-on-failure`
- Result: **55/56 passed**.
- Existing known failure remains unchanged:
  - `starter_scenarios_e2e_smoke`
  - reason: `trigger_roi.json` run-plan parser issue (`scenario camera.roi must include x, y, width, and height`), unrelated to this shared-parser refactor.

## Risk Assessment

Risk is low because:
- this change is scoped to parser code deduplication and direct wiring
- targeted tests for both consumers (validator + variant generator) pass
- agent integration smoke also passes
- no behavior-changing schema logic was altered

## Outcome

The repository now has one shared JSON parser used by both scenario validation and agent variant generation, removing duplicated parser code and reducing long-term maintenance risk.
