# SUMMARY — docs(agent): expand AGENTS handoff

## Goal
Make `AGENTS.md` significantly more comprehensive so future coding agents can pick up work quickly and consistently without re-discovery.

## What was implemented
- Reworked `AGENTS.md` into a fuller operations/handoff guide with clear sections:
  - project purpose and real-world framing
  - current status snapshot (recent commits + latest suite status)
  - architecture map by `src/` area
  - backend reality/limitations for `sim` vs `real_stub`
  - artifact contract (core, context, soak, optional files)
  - command cookbook (build, style, run, baseline, compare, kb)
  - strict user workflow rules (implementation -> verify -> commit)
  - definition-of-done checklist per commit
  - targeted test surface references
  - environment quirks and read-first file order
  - next-likely-work roadmap and guardrails

## Why
- Previous handoff was useful but still left implicit context spread across the repo.
- A stronger AGENTS file reduces onboarding time for the next agent and improves consistency in commits, testing, and communication style.
- Explicit checklists reduce regressions and process drift.

## Verification
- Confirmed key new sections exist in `AGENTS.md`.
- Confirmed user-required style constraints are preserved:
  - short commit messages
  - no commit numbers in message subjects
  - plain-language updates with implementation/verify/commit summary
- Docs-only change; no runtime code path affected.

## Files changed
- `AGENTS.md`
- `SUMMARY.md`
