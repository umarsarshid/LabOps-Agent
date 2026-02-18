# SUMMARY — 0093 `test(real): add MockNodeMapAdapter unit tests`

## Goal
Add real-backend parameter-bridge coverage using a dedicated mock node adapter, so key apply/mapping behaviors are tested in CI without requiring physical cameras.

## What I implemented

### 1) Added a new hardware-free unit/smoke test
File:
- `tests/backends/mock_node_map_adapter_smoke.cpp`

What this test introduces:
- `MockNodeMapAdapter` implementation of `INodeMapAdapter` used only for tests.
- `RecordingBackend` test backend that captures `SetParam` calls.
- Inline key-map fixture via `LoadParamKeyMapFromText(...)` (no file dependency).

Covered behaviors:
1. Enum mapping behavior:
   - validates generic key (`pixel_format`) maps to the expected SDK node (`PixelFormat`)
   - validates enum value normalization/canonical casing path
   - validates backend receives mapped node/value
2. Numeric range validation/clamping:
   - validates out-of-range numeric input is clamped by apply logic
   - validates readback evidence reflects clamped actual values
3. Strict vs best-effort behavior:
   - strict mode fails on unsupported input and returns actionable unsupported evidence
   - best-effort mode continues and preserves successful applies
4. ROI ordering logic:
   - validates apply order is `Width`, `Height`, `OffsetX`, `OffsetY`
     even when input order is offsets-first

Why:
- Existing coverage was strong but relied mainly on default in-memory adapter behavior.
- A dedicated mock adapter test isolates apply-logic contracts from adapter defaults and keeps the confidence signal hardware-independent.

### 2) Wired the new test into build/test system
File:
- `CMakeLists.txt`

What:
- Added a new smoke target:
  - `mock_node_map_adapter_smoke`

Why:
- Ensures the new coverage runs in CI and local regression loops.

### 3) Updated backend tests documentation
File:
- `tests/backends/README.md`

What:
- Added a bullet describing `mock_node_map_adapter_smoke.cpp` and exactly what it verifies.

Why:
- Keeps module-level test intent explicit for future maintainers.

## Verification performed

### Formatting
- `bash tools/clang_format.sh --check`
- Result: pass

### Build
- `cmake --build build`
- Result: pass

### Targeted tests
- `ctest --test-dir build -R "mock_node_map_adapter_smoke|real_apply_params_smoke" --output-on-failure`
- Result: pass (2/2)

### Full regression
- `ctest --test-dir build --output-on-failure`
- Result: pass (65/65)

## Net effect
- Key mapping/apply behavior is now explicitly covered by a dedicated mock adapter path.
- Confidence in real param-bridge logic is improved in camera-free CI environments.
