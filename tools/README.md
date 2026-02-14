# tools

Support files used by LabOps workflows and future operator tooling.

## Why this folder exists

Not every input belongs in runtime source code. Some inputs are operational
presets or helper data that scenarios can reference.

## Current contents

- `netem_profiles/`: declarative Linux network emulation presets that scenarios
  can reference via `netem_profile`.

## Connection to the project

This keeps repeatability data versioned in repo without coupling it to a
specific backend implementation stage.
