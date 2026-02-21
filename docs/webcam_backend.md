# Webcam Backend Policy (Bootstrap to Native)

This document explains where the webcam backend is today and where it is going.

## TL;DR

- OpenCV is the **bootstrap path** for webcam support.
- OpenCV is **not** the long-term architecture for production webcam control.
- The long-term path is native OS camera APIs:
  - Linux: V4L2
  - Windows: Media Foundation
  - macOS: AVFoundation

## Why OpenCV Is Here

OpenCV gets us usable webcam runs quickly:

- we can enumerate and open local cameras fast
- we can feed frames into LabOps events/metrics/bundle pipelines
- we can validate command and artifact contracts while native backends are built

This is valuable for early integration and workflow validation.

## Limits of OpenCV (Why It Is Transitional)

OpenCV is great for bootstrap, but weak for long-term hardware workflows:

- camera control support is inconsistent across platforms/drivers
- readback fidelity can be unclear for advanced controls
- transport-level and backend-specific diagnostics are limited
- behavior can differ across OpenCV builds/backends

For engineering-grade camera triage, native API control is more reliable.

## Target End State

LabOps webcam backend should use native API implementations as the primary path:

- Linux implementation under `src/backends/webcam/linux/`
- Windows implementation under `src/backends/webcam/windows/`
- macOS implementation under `src/backends/webcam/macos/`

OpenCV should eventually be:

- optional fallback for local bring-up only, or
- fully removed once native coverage reaches parity

## Contributor Rules (Effective Now)

1. Do not add new long-term camera capabilities only in OpenCV path.
2. New webcam feature work should target native API modules first.
3. If OpenCV is touched, changes should be limited to:
   - bootstrap stability
   - testability
   - compatibility shims needed while native path matures
4. Keep CLI contracts and artifact contracts backend-agnostic.

## Deprecation Plan

### Phase 1: Bootstrap (Current)

- OpenCV path enabled for quick local use.
- Deterministic mock-provider tests ensure stable classification behavior.
- Native modules exist as scaffolding/initial implementation targets.

Exit criteria:

- native Linux + Windows open/stream/select flows implemented
- artifact/event contracts equivalent for core run paths

### Phase 2: Native Parity

- implement native controls (width/height/fps/pixel format/exposure where available)
- stabilize native device selection and readback reporting
- add platform-native smoke guidance

Exit criteria:

- native paths pass normal run smoke checks on target platforms
- native path produces complete run bundles without OpenCV dependency

### Phase 3: OpenCV Demotion

- set OpenCV backend to fallback/off-by-default for contributors
- update docs and CI matrix focus to native path

Exit criteria:

- native path is default on supported platforms
- no critical workflows depend on OpenCV-specific behavior

### Phase 4: OpenCV Retirement (Optional)

- remove OpenCV implementation if fallback value is low
- keep deterministic test fixtures through backend-agnostic mock providers

## Verification Expectations During Transition

For any webcam backend change:

1. `bash tools/clang_format.sh --check`
2. `cmake --build build`
3. run focused webcam tests:
   - `ctest --test-dir build -R "webcam_.*smoke|run_webcam_selector_resolution_smoke" --output-on-failure`
4. if behavior-impacting: run full suite
   - `ctest --test-dir build --output-on-failure`

## Related Docs

- `src/backends/webcam/README.md` (module-level implementation notes)
- `docs/webcam_linux_troubleshooting.md` (Linux runtime triage notes)
- `docs/scenario_schema.md` (scenario contract)
- `docs/triage_bundle_spec.md` (artifact contract)
