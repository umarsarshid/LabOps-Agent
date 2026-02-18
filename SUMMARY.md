# Commit Summary

## Commit
`feat(events): emit transport anomaly events from real-counter heuristics`

## What I Implemented

This commit adds optional transport anomaly detection and wires it into both:
- `events.jsonl` (as machine-readable anomaly events)
- `summary.md` (as human-readable anomaly callouts)

In plain language:
- LabOps now checks real-device transport counters after a run.
- If counters cross heuristic thresholds (for example resend spikes), LabOps:
  - emits `TRANSPORT_ANOMALY` events
  - adds the same anomaly text into `Top Anomalies` in `summary.md`

## Why This Was Needed

We already capture transport counters in `run.json`, but raw numbers alone are
slow to triage. Engineers need automatic highlighting when counters look bad.

This commit turns counter evidence into actionable signals while staying
best-effort:
- no real-device metadata -> no anomaly checks
- counters unavailable -> no false failures
- only threshold hits generate events/callouts

## Detailed Changes

### 1) Added transport-anomaly heuristic module in events
Files:
- `src/events/transport_anomaly.hpp`
- `src/events/transport_anomaly.cpp`

What changed:
- Added `TransportAnomalyFinding` model.
- Added `DetectTransportAnomalies(const RunInfo&)`.
- Added deterministic heuristic thresholds:
  - `resends >= 50` -> `resend_spike_threshold`
  - `packet_errors >= 1` -> `packet_error_threshold`
  - `dropped_packets >= 1` -> `dropped_packet_threshold`
- Added clear summary text per finding.

Why:
- centralizes transport anomaly logic into one reusable place for both event
  emission and summary generation.

### 2) Added explicit event type for transport anomalies
Files:
- `src/events/event_model.hpp`
- `src/events/event_model.cpp`

What changed:
- Added `EventType::kTransportAnomaly`.
- JSON mapping now emits `"TRANSPORT_ANOMALY"`.

Why:
- gives downstream tooling a stable, queryable event type instead of relying on
  generic warning/info text parsing.

### 3) Wired anomaly detection + event emission into run pipeline
File:
- `src/labops/cli/router.cpp`

What changed:
- After base metric anomalies are built, router now calls
  `events::DetectTransportAnomalies(run_info)`.
- For each finding:
  - appends anomaly message into `top_anomalies` (so it appears in `summary.md`)
  - appends a `TRANSPORT_ANOMALY` event with payload:
    - `run_id`, `scenario_id`
    - `heuristic_id`
    - `counter`
    - `observed_value`
    - `threshold`
    - `summary`
- If transport anomalies exist, removes the placeholder metric-only message
  (`No notable anomalies detected by current heuristics.`) so summary output is
  not contradictory.

Why:
- fulfills milestone outcome directly: anomaly events exist and summary calls
  out transport anomalies when present.

### 4) Added smoke test coverage for new behavior
Files:
- `tests/events/transport_anomaly_smoke.cpp` (new)
- `tests/events/events_jsonl_smoke.cpp`
- `tests/core/event_json_test.cpp`
- `CMakeLists.txt`

What changed:
- New smoke verifies transport heuristics trigger findings only when counters
  are available and over threshold.
- `events_jsonl_smoke` now validates `TRANSPORT_ANOMALY` serialization in
  real JSONL output.
- Catch2 event mapping test updated for `kTransportAnomaly` string mapping.
- Added `transport_anomaly_smoke` test target; added
  `src/events/transport_anomaly.cpp` to `labops_events` library.

Why:
- locks in the new event contract and heuristic behavior.

### 5) Documentation updates
Files:
- `src/events/README.md`
- `tests/events/README.md`
- `src/labops/cli/README.md`
- `docs/triage_bundle_spec.md`

What changed:
- documented new transport anomaly module and event type.
- documented optional `TRANSPORT_ANOMALY` in run-flow event list.
- documented that summary now includes transport-counter threshold callouts for
  real runs when counters are available.

Why:
- keeps implementation and operator expectations aligned.

## Verification Performed

### Format
- `bash tools/clang_format.sh --check`
- Result: pass

### Build
- `cmake --build build`
- Result: pass

### Focused tests
- `ctest --test-dir build -R "events_jsonl_smoke|transport_anomaly_smoke|anomaly_detection_smoke|run_stream_trace_smoke|real_apply_mode_events_smoke" --output-on-failure`
- Result: pass (`5/5`)

### Full suite
- `ctest --test-dir build --output-on-failure`
- Result: pass (`61/61`)

## Risk Assessment

Low:
- additive event type + additive heuristic module
- best-effort behavior (no counters/no device -> no anomalies)
- full suite passed

## Outcome

LabOps now promotes transport counter spikes into first-class timeline evidence
and summary callouts, which speeds up real-network triage without making runs
brittle on SDKs that do not expose those counters.
