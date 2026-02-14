# tests/hostprobe

Host probe and evidence-redaction smoke tests.

## Why this folder exists

Host probing is a key part of every triage bundle. These tests protect both:
- evidence collection contracts
- privacy/safety behavior when redaction is enabled

## Current contents

- `redaction_smoke.cpp`:
  - builds a deterministic redaction context from controlled env vars
  - verifies host/user identifiers are removed from hostprobe JSON fields
  - verifies host/user identifiers are removed from raw NIC command text
  - verifies redaction placeholders are present after replacement

## Connection to the project

The lab assistant must produce shareable bundles quickly without leaking obvious
local identifiers. This test keeps the `--redact` behavior stable across
future refactors.
