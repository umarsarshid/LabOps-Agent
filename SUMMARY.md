# Commit Summary

## Commit
`refactor(cli): consolidate bundle manifest artifact collection`

## What I Implemented

I refactored `src/labops/cli/router.cpp` to remove duplicated bundle-manifest path assembly logic in two places:
- soak pause flow (when a soak run is safely paused)
- normal/final completion flow (when run finishes)

### Code Changes

1. Added helper `AppendArtifactIfPresent(...)`
- Location: `src/labops/cli/router.cpp`
- Behavior: appends a path only when it is non-empty and exists on disk.
- Why: this logic was repeated several times for optional artifacts (`config_verify.json`, `camera_config.json`, `config_report.md`, soak artifacts).

2. Added helper `BuildBundleManifestArtifactPaths(...)`
- Location: `src/labops/cli/router.cpp`
- Inputs:
  - required artifact paths (always included)
  - optional artifact paths (included only if present)
  - NIC raw artifact paths (`nic_*.txt`)
- Output: one consolidated vector used for `WriteBundleManifestJson(...)`.
- Why: pause and completion paths used near-identical artifact collection; centralizing removes drift risk and lowers maintenance cost.

3. Replaced duplicated manifest assembly in soak-pause path
- Old behavior: manually assembled vector + repeated optional checks + manual NIC insert.
- New behavior: build required list for pause artifacts, pass optional config artifacts to shared helper.
- Why: guarantees the same optional inclusion policy without repeating condition blocks.

4. Replaced duplicated manifest assembly in final-completion path
- Old behavior: manual vector assembly + repeated optional checks + soak-specific optional checks + NIC insert.
- New behavior:
  - build one `completion_optional_artifacts` list
  - append soak artifacts only when `--soak` is enabled
  - call shared helper for final list construction.
- Why: one source of truth for manifest artifact inclusion across both execution endings.

## Behavior/Contract Impact

No intended functional behavior change.
- Required artifacts remain required in each path.
- Optional artifacts are still included only if the file exists.
- NIC raw files are still appended to manifest inputs.

This is a maintainability/refactor commit to reduce duplication and lower regression risk when artifact contract evolves.

## Verification Performed

### Formatting
- `bash tools/clang_format.sh --check`
- Result: pass.

### Build
- `cmake -S . -B build`
- `cmake --build build`
- Result: pass.

### Targeted tests for touched behavior
- `ctest --test-dir build -R "(run_stream_trace_smoke|bundle_layout_consistency_smoke|soak_checkpoint_resume_smoke|baseline_capture_smoke)" --output-on-failure`
- Result: 4/4 passed.

### Full suite sanity run
- `ctest --test-dir build --output-on-failure`
- Result: 54/55 passed.
- One failing test: `starter_scenarios_e2e_smoke`.
- Failure reason seen in logs: `labops run scenarios/trigger_roi.json` fails during run-plan parsing with:
  - `scenario camera.roi must include x, y, width, and height`
- Notes:
  - this failure occurs before manifest-writing stage and is unrelated to this manifest-collection refactor.
  - manual spot-check confirmed:
    - `./build/labops validate scenarios/trigger_roi.json` passes
    - `./build/labops run scenarios/trigger_roi.json ...` fails with the same run-plan parser error.

## Why This Refactor Matters

- Prevents pause/final manifest logic from drifting over time.
- Reduces copy-paste condition blocks, making future artifact additions safer.
- Keeps manifest behavior explicit while lowering cognitive load in a very large run pipeline function.

## Files Changed

- `src/labops/cli/router.cpp`
