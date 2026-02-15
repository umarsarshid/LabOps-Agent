# Scenario JSON Schema

This document defines the scenario JSON contract used by `labops run` and
future agent execution workflows.

## Purpose

Scenario files must be explicit, repeatable, and machine-validated so runs can
be reproduced and compared over time.

Design goals:

- deterministic execution
- clear camera/sim configuration
- explicit pass/fail thresholds
- support for "one variable at a time" experimentation

## Top-Level Shape

A scenario JSON document is an object with these top-level sections:

- `schema_version`
- `scenario_id`
- `description`
- `tags`
- `duration`
- `backend`
- `device_selector`
- `netem_profile`
- `camera`
- `sim_faults`
- `thresholds`
- `oaat` (one-at-a-time guidance)

## Field Definitions

### `schema_version` (required)

- Type: string
- Purpose: schema contract version for forward compatibility.
- Example: `"1.0"`

### `scenario_id` (required)

- Type: string
- Purpose: stable identifier used in artifacts/reports.
- Constraint: lowercase slug (`[a-z0-9_-]+`).
- Example: `"stream_baseline_1080p"`

### `description` (optional)

- Type: string
- Purpose: human-readable context for operators/reviewers.

### `tags` (optional)

- Type: array of strings
- Purpose: filtering/grouping in CI dashboards and triage.
- Example: `["baseline", "ethernet", "long_run"]`

### `duration` (required)

- Type: object
- Required fields:
  - `duration_ms` (integer, `> 0`)
- Purpose: run length definition.

### `backend` (optional)

- Type: string
- Purpose: selects execution backend implementation.
- Allowed values:
  - `"sim"` (default when field is omitted)
  - `"real_stub"` (deterministic non-SDK stub path useful for integration
    contract testing)

### `device_selector` (optional)

- Type: string
- Purpose: deterministic camera selection for real-backend runs.
- Allowed keys:
  - `serial:<value>`
  - `user_id:<value>`
  - optional `index:<n>` (0-based) for tie-breaks
- Clause format: comma-separated `key:value` pairs, for example:
  - `"serial:SN-1001"`
  - `"user_id:Primary,index:0"`
- Constraints:
  - cannot include both `serial` and `user_id` in one selector
  - key names must be one of `serial`, `user_id`, `index`
  - index must be a non-negative integer
  - currently requires `backend: "real_stub"`
- CLI override:
  - `labops run ... --device <selector>` overrides scenario `device_selector`
  - same override also applies to `labops baseline capture ... --device ...`

### `netem_profile` (optional)

- Type: string
- Purpose: reference to a named Linux network impairment preset.
- Constraint: lowercase slug (`[a-z0-9_-]+`).
- Resolution: `labops validate <scenario>` checks the profile exists under
  `tools/netem_profiles/<netem_profile>.json`.
- Runtime note: in this milestone the value is validated only (no execution).

### `camera` (required)

- Type: object
- Purpose: camera/runtime parameters applied before streaming.
- Fields:
  - `device_id` (string, optional)
  - `pixel_format` (string, optional)
  - `width` (integer, optional, `> 0`)
  - `height` (integer, optional, `> 0`)
  - `fps` (integer, optional, `> 0`)
  - `exposure_us` (integer, optional, `>= 0`)
  - `gain_db` (number, optional)
  - `trigger_mode` (string, optional)
    - allowed: `"free_run"`, `"software"`, `"hardware"`
  - `roi` (object, optional)
    - `x` (integer, `>= 0`)
    - `y` (integer, `>= 0`)
    - `width` (integer, `> 0`)
    - `height` (integer, `> 0`)
  - `network` (object, optional)
    - `packet_size_bytes` (integer, optional, `> 0`)
    - `inter_packet_delay_us` (integer, optional, `>= 0`)

### `sim_faults` (optional)

- Type: object
- Purpose: deterministic fault injection for simulation/testing.
- Fields:
  - `seed` (integer, optional, `>= 0`)
  - `jitter_us` (integer, optional, `>= 0`)
  - `drop_every_n` (integer, optional, `>= 0`)
  - `drop_percent` (integer, optional, `0..100`)
  - `burst_drop` (integer, optional, `>= 0`)
  - `reorder` (integer, optional, `>= 0`)
  - `disconnect_at_ms` (integer, optional, `>= 0`)
  - `disconnect_duration_ms` (integer, optional, `>= 0`)

Notes:

- `drop_every_n=0` disables periodic dropping.
- `drop_percent=0` disables probabilistic dropping.
- use `seed` for deterministic replay.

### `thresholds` (required)

- Type: object
- Purpose: pass/fail expectations for automated triage.
- Runtime behavior: `labops run` evaluates configured thresholds against
  computed metrics and returns non-zero when any threshold is violated.
- Fields:
  - `min_avg_fps` (number, optional)
  - `max_drop_rate_percent` (number, optional)
  - `max_inter_frame_interval_p95_us` (number, optional)
  - `max_inter_frame_jitter_p95_us` (number, optional)
  - `max_disconnect_count` (integer, optional, `>= 0`)

At least one threshold should be present.

### `oaat` (optional, one-at-a-time)

- Type: object
- Purpose: guides agent-controlled one-variable-at-a-time experiments.
- Fields:
  - `enabled` (boolean, required when `oaat` exists)
  - `variables` (array, optional)
    - each item:
      - `path` (string, required)
        - dotted path to target field, e.g. `"camera.roi.width"`
      - `values` (array, required, at least 1)
      - `mode` (string, optional)
        - allowed: `"replace"` (default)
  - `max_trials` (integer, optional, `> 0`)
  - `stop_on_first_failure` (boolean, optional)

## Minimal Valid Example

```json
{
  "schema_version": "1.0",
  "scenario_id": "baseline_smoke",
  "backend": "real_stub",
  "device_selector": "serial:SN-1001,index:0",
  "netem_profile": "jitter_light",
  "duration": {
    "duration_ms": 10000
  },
  "camera": {
    "fps": 30,
    "pixel_format": "mono8",
    "trigger_mode": "free_run"
  },
  "thresholds": {
    "min_avg_fps": 28.0,
    "max_drop_rate_percent": 1.0
  }
}
```

## Full Example

```json
{
  "schema_version": "1.0",
  "scenario_id": "trigger_roi_stress",
  "description": "Validate stream behavior under ROI + trigger + injected transport faults.",
  "netem_profile": "loss_medium",
  "tags": ["stress", "trigger", "roi", "sim"],
  "duration": {
    "duration_ms": 600000
  },
  "camera": {
    "device_id": "cam_001",
    "pixel_format": "mono8",
    "width": 1920,
    "height": 1080,
    "fps": 60,
    "exposure_us": 8000,
    "gain_db": 3.0,
    "trigger_mode": "hardware",
    "roi": {
      "x": 100,
      "y": 120,
      "width": 1280,
      "height": 720
    },
    "network": {
      "packet_size_bytes": 9000,
      "inter_packet_delay_us": 200
    }
  },
  "sim_faults": {
    "seed": 1234,
    "jitter_us": 500,
    "drop_every_n": 25,
    "drop_percent": 2,
    "burst_drop": 0,
    "reorder": 3
  },
  "thresholds": {
    "min_avg_fps": 55.0,
    "max_drop_rate_percent": 3.0,
    "max_inter_frame_interval_p95_us": 25000,
    "max_inter_frame_jitter_p95_us": 5000
  },
  "oaat": {
    "enabled": true,
    "max_trials": 12,
    "stop_on_first_failure": false,
    "variables": [
      {
        "path": "camera.roi.width",
        "values": [1280, 960, 640]
      },
      {
        "path": "camera.trigger_mode",
        "values": ["free_run", "hardware"]
      },
      {
        "path": "sim_faults.drop_percent",
        "values": [0, 1, 2, 5]
      }
    ]
  }
}
```

## Validation Rules Summary

- required sections: `schema_version`, `scenario_id`, `duration`, `camera`,
  `thresholds`
- numeric values must be non-negative unless explicitly marked `> 0`
- `drop_percent` must be in `[0, 100]`
- `tags` entries must be non-empty
- if `oaat` exists and `enabled=true`, each `variables` item must include both
  `path` and non-empty `values`

## Compatibility Notes

- New fields may be added as optional in future versions.
- Breaking changes require a new `schema_version`.
