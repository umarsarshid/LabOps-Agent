# src/labops/cli

This folder contains CLI routing and command dispatch behavior.

## Why this folder exists

As commands grow (`run`, `validate`, `version`, and later `bundle`, `agent`, `baseline`), command wiring needs a focused home so `main` stays minimal and behavior remains testable.

## Current responsibilities

- Map argv input to known subcommands.
- Enforce usage errors consistently.
- Preserve stable exit-code semantics for automation.
- Print deterministic command outputs.
- Parse run artifact options (currently `--out <dir>`).

## Design intent

The router is intentionally explicit rather than clever. Early explicit branching is easier to audit, easier to test, and safer for a hardware-focused team where reproducibility matters more than abstraction density.

## Connection to the project

This is where reproducibility starts. If CLI contracts are ambiguous, every downstream test run and triage report becomes harder to trust.
