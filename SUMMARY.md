# Commit Summary

## Commit
`refactor(soak): extract checkpoint/frame-cache persistence module`

## What I Implemented

I extracted soak checkpoint and frame-cache persistence logic out of the CLI monolith (`router.cpp`) into a dedicated module so it can be maintained and tested independently.

## Why This Change

Previously, soak storage behavior (checkpoint JSON writing/loading and frame-cache appends/loads) lived inline inside `src/labops/cli/router.cpp`. That made run-flow changes risky because persistence concerns were tightly coupled to command orchestration.

This refactor separates concerns:
- `router.cpp` now orchestrates run flow.
- `labops::soak` now owns durable soak state I/O.

That makes pause/resume logic easier to reason about, safer to modify, and easier to test in isolation.

## Files Added

### `src/labops/soak/checkpoint_store.hpp`
- Added a dedicated soak persistence API with:
  - `CheckpointStatus`
  - `CheckpointState`
  - `WriteCheckpointJson(...)`
  - `WriteCheckpointArtifacts(...)`
  - `LoadCheckpoint(...)`
  - `AppendFrameCache(...)`
  - `LoadFrameCache(...)`
- Why: provide a stable module contract for soak persistence primitives.

### `src/labops/soak/checkpoint_store.cpp`
- Moved and encapsulated the soak persistence implementation:
  - checkpoint JSON serialization
  - checkpoint history/latest file writing
  - checkpoint loading + validation
  - frame-cache append/load parsing
- Uses shared `core::EscapeJson(...)` for JSON escaping consistency.
- Why: remove heavy persistence code from CLI router and keep behavior centralized.

### `src/labops/soak/README.md`
- Documented what this module is, why it exists, and how it connects to run flow.
- Why: improve discoverability for future engineers.

### `tests/labops/soak_checkpoint_store_smoke.cpp`
- Added module-level smoke test for direct roundtrip verification:
  - checkpoint write/load
  - frame-cache append/load
- Why: prove soak persistence can be verified independently from CLI routing.

## Files Updated

### `src/labops/cli/router.cpp`
- Removed inline soak persistence block (checkpoint/frame-cache types + functions).
- Added include for `labops/soak/checkpoint_store.hpp`.
- Rewired call sites to module functions:
  - `soak::LoadCheckpoint(...)`
  - `soak::LoadFrameCache(...)`
  - `soak::AppendFrameCache(...)`
  - `soak::WriteCheckpointArtifacts(...)`
- Updated type usage to `soak::CheckpointState` / `soak::CheckpointStatus`.
- Why: keep router focused on orchestration and reduce monolithic complexity.

### `CMakeLists.txt`
- Added `labops_soak` library target.
- Linked `labops` binary against `labops_soak`.
- Added `labops_soak` to `LABOPS_CLI_SMOKE_LIBRARIES` so router-linked smoke tests resolve module symbols.
- Added new smoke target: `soak_checkpoint_store_smoke`.
- Why: make the new module a first-class build/test unit.

### `src/labops/README.md`
- Added note that CLI-owned support modules (e.g., soak persistence) live under `src/labops/` subfolders.
- Why: keep folder docs aligned with code structure.

## Behavior Impact

No intended runtime contract change.
- Soak pause/resume artifacts remain the same:
  - `soak_checkpoint.json`
  - `checkpoints/checkpoint_<n>.json`
  - `soak_frames.jsonl`
- Existing CLI flows continue to use same soak semantics, now via module calls.

## Verification Performed

### Formatting
- `bash tools/clang_format.sh --check` (tracked files): pass.
- `clang-format --dry-run --Werror` on new soak files + new smoke test: pass.

### Build
- `cmake -S . -B build`: pass.
- `cmake --build build`: pass.

### Targeted Tests (refactor scope)
- `ctest --test-dir build -R "(soak_checkpoint_store_smoke|soak_checkpoint_resume_smoke|run_stream_trace_smoke|bundle_layout_consistency_smoke)" --output-on-failure`
- Result: 4/4 passed.

### Full Suite Regression Check
- `ctest --test-dir build --output-on-failure`
- Result: 55/56 passed.
- One known unrelated failure remains unchanged:
  - `starter_scenarios_e2e_smoke`
  - failing due `trigger_roi.json` run-plan parse error:
    `scenario camera.roi must include x, y, width, and height`

## Result

The soak persistence subsystem is now a dedicated module with direct test coverage, and CLI run orchestration is slimmer and easier to maintain.
