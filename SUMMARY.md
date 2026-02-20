# LabOps Summary

## Commit: docs(webcam): document OpenCV bootstrap transition

Date: 2026-02-20

### Goal
Implement milestone `0106` by clearly documenting that the OpenCV webcam backend is a bootstrap path, not the long-term architecture, and describing a deprecation/migration plan to native platform APIs.

### Implemented

1. Added a dedicated webcam backend policy doc
- Added: `docs/webcam_backend.md`
- Includes:
  - explicit statement that OpenCV is bootstrap/transitional only
  - long-term target backends (V4L2, Media Foundation, AVFoundation)
  - practical limits of OpenCV for production-grade camera control/diagnostics
  - contributor rules to prevent long-term feature investment in OpenCV-only paths
  - phased deprecation plan with clear exit criteria
  - verification checklist for webcam backend changes

Why:
- removes ambiguity for contributors about where to build durable features
- reduces architectural drift and “temporary forever” risk
- sets a concrete path from bootstrap to production-ready native implementations

2. Linked the new policy in docs index
- Updated: `docs/README.md`
- Added `webcam_backend.md` in expected contents list

Why:
- makes the transition policy discoverable during onboarding and handoff
- ensures contributors can quickly find the governance doc

### Files changed
- `docs/webcam_backend.md`
- `docs/README.md`
- `SUMMARY.md`

### Verification

1. Formatting
- Command: `bash tools/clang_format.sh --check`
- Result: passed

2. Build
- Command: `cmake --build build`
- Result: passed

3. Focused regression checks
- Command:
  - `ctest --test-dir build -R "list_backends_smoke|run_webcam_selector_resolution_smoke" --output-on-failure`
- Result: passed (`2/2`)

### Outcome
Contributors now have a single, explicit source of truth that OpenCV is transitional and that native platform webcam backends are the long-term implementation path.
