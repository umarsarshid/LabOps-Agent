# SUMMARY — 0096 `docs(real): add real-backend troubleshooting page`

## Goal
Create a practical real-backend troubleshooting runbook that reads like internal AE/QA notes, focused on the most common lab blockers:
- device not found
- timeouts
- trigger confusion
- GigE MTU/packet issues

## Implementation

### 1) Added a dedicated troubleshooting runbook
File:
- `docs/real_backend_troubleshooting.md`

What:
- Added a symptom-based guide with a short first-pass triage flow.
- Added dedicated sections for each requested issue class:
  - Device not found
  - Timeouts / intermittent frame gaps
  - Trigger confusion
  - GigE MTU / packet-size issues
- For each section, documented:
  - typical signs
  - likely causes
  - concrete checks (commands + artifacts)
  - practical fix pattern
  - evidence to attach for escalation
- Added an escalation checklist for handoffs.

Why:
- Engineers need fast, repeatable triage steps in production-like lab incidents.
- A symptom-first runbook reduces back-and-forth and improves quality of handoff evidence.

### 2) Linked the runbook from core docs entry points
Files:
- `docs/README.md`
- `docs/real_backend_setup.md`
- `README.md`

What:
- Added references so the troubleshooting doc is discoverable from:
  - top-level project README
  - docs index
  - real-backend setup guide

Why:
- Good runbooks only help if engineers can find them quickly during failures.

## Verification

Link/path checks:
- `rg -n "real_backend_troubleshooting.md" README.md docs/README.md docs/real_backend_setup.md`
- confirmed all expected references exist.
- `test -f docs/real_backend_troubleshooting.md` -> doc exists.

Repo hygiene checks:
- `bash tools/clang_format.sh --check` -> pass
- `cmake --build build` -> pass

## Outcome
- Real-backend troubleshooting guidance now exists as a single operational runbook.
- The doc is written in practical AE/QA style and anchored to actual LabOps artifacts.
- Entry-point docs now route engineers to the runbook directly.
