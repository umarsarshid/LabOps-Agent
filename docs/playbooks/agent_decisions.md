# Agent Decision Flow

This document explains, in plain language, how the current LabOps agent decides
what to do next.

Goal: a human should be able to predict the next agent action from the same
inputs.

## Why this exists

If the decision path is not explicit, two engineers can read the same data and
expect different next steps. This doc makes the decision order concrete so
triage is repeatable across people and CI environments.

## Inputs That Drive Decisions

The current decision path uses:
- symptom input (for playbook selection)
- base scenario path
- OAAT attempt history (`configs_tried` / `results_table`)
- confidence score
- stop-condition thresholds

Relevant modules:
- `src/agent/playbook.cpp`
- `src/agent/variant_generator.cpp`
- `src/agent/stop_conditions.cpp`
- `src/agent/engineer_packet_writer.cpp`

## Step 1: Choose Playbook By Symptom

Function:
- `SelectPlaybookForSymptom(...)`

Current supported symptom mapping:
- `dropped_frames`
- aliases: `frame_drops`, `drops`

Selected playbook (ordered knobs):
1. `packet_delay_ms`
2. `fps`
3. `roi_enabled`
4. `reorder_percent`
5. `loss_percent`

Prediction rule:
- If symptom is dropped-frames-like, this playbook is always selected.
- If symptom is unknown, selection fails with actionable error and no run plan
  is created.

## Step 2: Generate One-Variable Variants

Function:
- `OaatVariantGenerator::Generate(...)`

Output contract:
- `out/agent_runs/` (default)
- one scenario file per knob, each with exactly one mutation
- `out/agent_runs/variants_manifest.json`

Prediction rule:
- Variant files are generated in playbook order.
- If base scenario is `dropped_frames.json`, filenames follow:
  - `dropped_frames__packet_delay_ms.json`
  - `dropped_frames__fps.json`
  - `dropped_frames__roi_enabled.json`
  - `dropped_frames__reorder_percent.json`
  - `dropped_frames__loss_percent.json`

## Step 3: Execution Order (What Agent Tries Next)

Current deterministic order is the playbook order above.

Human prediction rule:
- Look at `configs_tried` sequence.
- The next attempt is the first playbook knob not yet tried.
- If none remain, the loop should stop or escalate to human depending on stop
  policy/state.

Example:
- Already tried: `packet_delay_ms`, `fps`
- Next knob: `roi_enabled`

## Step 4: Stop Conditions (Deterministic Priority)

Function:
- `EvaluateStopConditions(...)`

Stop checks run in fixed order:
1. `max_runs`
2. `single_variable_flip`
3. `confidence_threshold`
4. `stable_repro_rate`

Prediction rule:
- If multiple conditions are true, the first in this list wins.
- The agent returns one reason plus one explanation string.

### Condition meanings

- `max_runs`:
  - stop when run count reaches configured cap.
- `single_variable_flip`:
  - stop when one variable shows fail at one value and pass at another value.
- `confidence_threshold`:
  - stop when confidence score reaches threshold.
- `stable_repro_rate`:
  - stop when recent decisive runs reproduce failure at/above threshold.

## Step 5: Engineer Packet At Stop

Function:
- `WriteEngineerPacketMarkdown(...)`

Output:
- `engineer_packet.md`

The packet includes:
- repro steps
- configs tried
- what changed
- what we ruled out
- ranked hypotheses + evidence links

Evidence links are exact artifact paths, including diff files when available:
- `run.json`
- `events.jsonl`
- `metrics.json`
- `summary.md`
- `diff.json`
- `diff.md`

## Predict The Next Action (Algorithm)

Use this deterministic logic:

1. Normalize symptom and select a playbook.
2. Build ordered knob list from that playbook.
3. Evaluate stop conditions in fixed priority order.
4. If stop condition is true:
   - stop and write `engineer_packet.md`.
5. If stop condition is false:
   - pick the first playbook knob not present in `configs_tried`.
   - run that single-knob variant next.
6. Repeat until stop reason is no longer `continue`.

This means two people with the same symptom, history, and stop config should
predict the exact same next run.

## Quick Prediction Checklist

Use this checklist to predict next agent action:
1. Normalize symptom and map to playbook.
2. Read ordered knob list for that playbook.
3. Check which knobs are already in `configs_tried`.
4. Evaluate stop conditions in fixed priority order.
5. If no stop condition fires, pick the first untried knob as next attempt.
6. If stop triggers, expect packet generation with exact evidence links.

## Known Scope Limits (Current Milestone State)

- Only dropped-frames playbook is currently registered.
- OAAT planning is deterministic and rule-based (not LLM-based).
- The decision path is designed to be explainable first, then extensible.
