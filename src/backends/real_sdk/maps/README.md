# src/backends/real_sdk/maps

Data-only mapping files for real-backend SDK integration.

## Why this folder exists

Generic LabOps parameter keys (for example `exposure`, `frame_rate`) should map
to SDK node names without requiring C++ code edits for every vendor naming
change. Keeping mappings in JSON allows rapid updates by integration engineers.

## Current contents

- `param_key_map.json`: maps generic parameter keys to SDK node names.
  - initial keys include:
    - `exposure`
    - `gain`
    - `pixel_format`
    - `roi_width`
    - `roi_height`
    - `roi_offset_x`
    - `roi_offset_y`
    - `roi` (legacy alias)
    - `trigger_mode`
    - `trigger_source`
    - `frame_rate`

## Connection to the project

This folder is the configuration side of the parameter bridge:
- code loads and validates the map
- runtime logic can query/update mapping behavior by editing JSON instead of
  modifying core run orchestration code.
