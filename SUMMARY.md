# test(webcam/linux): add manual smoke target

## Why this change
Developers need a one-command check to confirm a Linux machine can run the native webcam path end-to-end (connect, stream, write bundle) without relying on CI.

## What was implemented

### 1) Added Linux manual webcam smoke target in CMake
File:
- `CMakeLists.txt`

Changes:
- Added a new developer-only custom build target:
  - `webcam_linux_smoke_manual`
- Linux behavior:
  - creates output root `tmp/manual_webcam_linux_smoke`
  - runs:
    - `labops run tests/manual/webcam_linux_smoke_scenario.json --out tmp/manual_webcam_linux_smoke`
- Non-Linux behavior:
  - target still exists, but prints a clear Linux-only message and exits cleanly.

Why:
- Gives Linux developers one command for lab bring-up.
- Keeps cross-platform developer experience clean (no missing target confusion).
- Stays out of CI because it is a custom target, not a `ctest` test.

### 2) Added dedicated Linux webcam manual scenario fixture
File:
- `tests/manual/webcam_linux_smoke_scenario.json`

Changes:
- New 5-second webcam scenario with:
  - `backend: "webcam"`
  - `apply_mode: "best_effort"`
  - webcam selector `index: 0`
  - requested format hints (`1280x720`, `30 fps`, `MJPG`)
  - permissive thresholds for bring-up diagnostics.

Why:
- Provides a stable, checked-in fixture for manual Linux validation.
- Avoids ad-hoc local JSON files when onboarding or troubleshooting.

### 3) Updated manual testing docs
Files:
- `tests/manual/README.md`
- `tests/README.md`

Changes:
- Documented new Linux webcam manual command.
- Documented output path:
  - `tmp/manual_webcam_linux_smoke/<run_id>/`
- Clarified non-Linux fallback behavior for the target.

Why:
- Keeps manual workflow discoverable and consistent for future engineers.

## Verification performed

1. Formatting:
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build:
- `cmake --build build`
- Result: passed

3. Scenario validation:
- `./build/labops validate tests/manual/webcam_linux_smoke_scenario.json`
- Result: `valid: tests/manual/webcam_linux_smoke_scenario.json`

4. Manual target command:
- `cmake --build build --target webcam_linux_smoke_manual`
- Result on this non-Linux host: prints Linux-only message and succeeds.

5. Full regression:
- `ctest --test-dir build --output-on-failure`
- Result: passed (`83/83`)

## Outcome
A developer can now run one command (`webcam_linux_smoke_manual`) to quickly validate Linux webcam bring-up and produce a normal run bundle, while CI remains hardware-independent.
