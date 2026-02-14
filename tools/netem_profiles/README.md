# tools/netem_profiles

Linux `netem` profile definitions for scenario references.

## Why this folder exists

Some camera issues only appear under network impairment. These profiles provide
named, versioned presets so scenarios can point to a shared definition instead
of embedding ad-hoc values.

## Current status

- Profiles are definitions only in this milestone.
- `labops` validates profile references exist.
- No command execution is performed yet.

## Profile files

- `jitter_light.json`: mild delay+jitter profile.
- `loss_medium.json`: moderate packet loss profile.
- `reorder_light.json`: light packet reorder profile.

## Connection to the project

This is the foundation for a future netem harness where the agent can run
controlled network experiments with deterministic profile names.
