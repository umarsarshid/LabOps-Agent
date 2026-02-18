# SUMMARY — docs update: future agent behavior

## What was implemented
- Updated `AGENTS.md` under **User Workflow Requirements** to explicitly require:
  - short, clean commit messages
  - concise single-line subjects (prefer `type(scope): intent`)
  - avoiding long commit bodies unless the user asks
  - running `bash tools/clang_format.sh --check` before commit

## Why
- Recent commits became too long/noisy in message format.
- This makes future handoffs and git history easier to scan.
- It prevents formatting regressions from reaching CI.

## Verification
- Reviewed `AGENTS.md` to confirm new rules are present in the workflow list.
- Confirmed wording includes concrete examples for desired commit style.

## Commit scope
- Docs/process-only change.
- No runtime code behavior changed.
