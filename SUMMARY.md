# docs(webcam): add Linux troubleshooting notes

## Why this change
Linux webcam issues were spread across logs and backend docs but we did not have a dedicated operator-facing troubleshooting playbook like the real backend page. Engineers needed a quick, practical guide for common Linux webcam failures.

## What was implemented

### 1) Added a dedicated Linux webcam troubleshooting playbook
File:
- `docs/webcam_linux_troubleshooting.md`

Changes:
- Added an AE/QA-style troubleshooting document with:
  - quick triage flow (backend status -> discovery -> manual smoke -> key artifacts)
  - symptom-driven sections for:
    - permissions on `/dev/video*`
    - busy device / camera in use
    - driver-clamped FPS (requested vs actual)
    - auto exposure causing timing jitter
    - native V4L2 fallback to OpenCV path
  - actionable checks, fix patterns, and evidence checklist for escalation
  - related docs links

Why:
- Gives hardware/software engineers a practical runbook to reduce back-and-forth and speed root-cause isolation.

### 2) Linked troubleshooting doc from webcam policy page
File:
- `docs/webcam_backend.md`

Changes:
- Added `docs/webcam_linux_troubleshooting.md` under related docs.

Why:
- Keeps policy and operational troubleshooting connected in one navigation path.

### 3) Linked troubleshooting doc from docs index
File:
- `docs/README.md`

Changes:
- Added Linux webcam troubleshooting playbook entry.

Why:
- Improves discoverability from the documentation root.

### 4) Linked troubleshooting doc from module README
File:
- `src/backends/webcam/README.md`

Changes:
- Added related docs section linking to webcam policy and Linux troubleshooting playbook.

Why:
- Keeps implementation docs connected to operator runbooks.

## Verification performed

1. Formatting/style gate
- `bash tools/clang_format.sh --check`
- Result: passed

2. Build verification
- `cmake --build build`
- Result: passed

## Outcome
Linux webcam troubleshooting now has a dedicated, internal-note-style runbook with practical checks and evidence expectations, aligned with the tone and structure of the real backend troubleshooting documentation.
