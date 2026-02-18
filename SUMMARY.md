# SUMMARY — 0095 `test(integration): add manual real camera smoke target`

## Goal
Add one manual command developers can run to quickly verify local real-backend lab setup (connect -> stream 5s -> dump config -> exit) without putting camera-dependent checks into CI.

## Implementation

### 1) Added a dedicated manual smoke scenario
Files:
- `tests/manual/real_camera_smoke_scenario.json`

What:
- Added a 5-second scenario intended for lab bring-up:
  - `backend: real_stub`
  - `apply_mode: best_effort`
  - camera defaults for free-run streaming (`fps`, `pixel_format`, `trigger_mode`, exposure/gain)
  - minimal thresholds (`min_avg_fps`, permissive drop rate, no disconnects)

Why:
- Gives developers a stable, checked-in scenario for fast manual validation.
- Avoids ad-hoc per-engineer scenario files for the same bring-up task.

### 2) Added a non-CTEST manual CMake target
File:
- `CMakeLists.txt`

What:
- Added custom target: `real_camera_smoke_manual`
- Target behavior:
  - ensures `tmp/manual_real_camera_smoke` exists
  - runs:
    - `labops run tests/manual/real_camera_smoke_scenario.json --out tmp/manual_real_camera_smoke`
- Added clear comments explaining why this is a custom target and not `add_test(...)`.

Why:
- Delivers the requested one-command operator flow.
- Keeps hardware/lab checks out of CI while still making them easy to run locally.

### 3) Added manual-test folder documentation
File:
- `tests/manual/README.md`

What:
- Documented purpose of `tests/manual/`.
- Documented one-command usage and expected output path.
- Documented that this target is intentionally not part of `ctest`.
- Included note for multi-camera labs to run equivalent CLI with `--device`.

Why:
- Makes manual workflow discoverable for new engineers.
- Prevents confusion about why this target does not appear in CI test output.

### 4) Updated existing docs to surface the new workflow
Files:
- `tests/README.md`
- `docs/real_backend_setup.md`

What:
- Added references to `tests/manual/` and the `real_camera_smoke_manual` command.
- Added plain-language flow and output location for manual bring-up verification.

Why:
- Keeps setup and testing docs aligned with the new command.
- Reduces time to first successful lab validation.

## Verification

Formatting:
- `bash tools/clang_format.sh --check` -> pass

Default build (existing local build tree):
- `cmake --build build` -> pass

Targeted smoke check (regression guard):
- `ctest --test-dir build -R list_backends_smoke --output-on-failure` -> pass

Manual target end-to-end validation in real-enabled fixture build:
1. Created local fake SDK dirs:
   - `tmp/fake_vendor_sdk/include`
   - `tmp/fake_vendor_sdk/lib`
2. Configured real-enabled build:
   - `cmake -S . -B tmp/build-real-manual -DLABOPS_ENABLE_REAL_BACKEND=ON -DVENDOR_SDK_ROOT:PATH="$(pwd)/tmp/fake_vendor_sdk"`
3. Ran one-command manual target:
   - `cmake --build tmp/build-real-manual --target real_camera_smoke_manual`
4. Confirmed success and artifact output:
   - `tmp/manual_real_camera_smoke/run-1771448971550/`
   - includes `camera_config.json`, `config_verify.json`, `config_report.md`, `events.jsonl`, `metrics.*`, `summary.md`, `report.html`, `bundle_manifest.json`

## Outcome
- Developers now have a single command to perform a quick real-backend smoke flow.
- The workflow stays manual and out of CI by design.
- Documentation points to the command and output bundle location.
