# Code Smartness Level-Up Plan (No Functional Change)

This document captures architecture and maintainability upgrades that improve code quality, safety, and velocity **without changing CLI behavior, artifact formats, or test expectations**.

## Scope and guardrails

- Keep all existing CLI contracts the same (`run`, `validate`, `compare`, `baseline`, `kb`, `list-*`).
- Keep artifact names and file formats the same unless a change is explicitly additive.
- Keep exit codes and error semantics stable.
- Treat current test suite as behavioral guardrails.

## What I learned in the full code pass

- `src/labops/cli/router.cpp` is the orchestration center and is doing option parsing, scenario loading, backend setup, stream control, soak checkpointing, metrics, artifact writing, and command dispatch in one translation unit.
- There is strong module intent already (`artifacts/`, `backends/`, `metrics/`, `events/`, `hostprobe/`, `soak/`, `agent/`), but some cross-cutting concerns are still implemented in multiple places (JSON helpers, file writing patterns, path resolution and parsing patterns).
- Real-backend logic is already thoughtfully layered (`real_backend`, `sdk_context`, `stream_session`, `frame_provider`, `apply_params`, `param_key_map`, `error_mapper`) and is a good base for incremental cleanup.
- Host and soak features are solid functionally, but their internals would benefit from more decomposition for readability/testing.

## Priority order (best payoff vs risk)

1. Break down run orchestration into explicit stage modules.
2. Introduce a typed scenario run model (single parse source for run planning).
3. Consolidate artifact list assembly and writer plumbing into a small artifact pipeline abstraction.
4. Split host probe platform logic into per-platform files.
5. Extract reconnect/disconnect handling into a dedicated policy/state machine.
6. Standardize shared low-level utilities (JSON/time/path/atomic file writes).

## Recommended improvements

## 1) Split run pipeline from CLI command plumbing

**Current hotspot**
- `src/labops/cli/router.cpp`

**Issue**
- `ExecuteScenarioRunInternal` has many responsibilities and a large control-flow surface (normal run, soak run, resume, disconnect retries, artifact output).

**Refactor (no behavior change)**
- Keep public `ExecuteScenarioRun(...)` signature unchanged.
- Internally split into stage functions and a small `RunExecutionContext` struct:
  - `PrepareRunContext`
  - `InitializeArtifacts`
  - `ConfigureBackend`
  - `ExecuteStreaming`
  - `FinalizeMetricsAndReports`
  - `EmitFinalConsoleSummary`

**Why this helps**
- Easier code review, lower regression risk for future features.
- Stage-level unit tests become possible without invoking the full command path.

**Verification**
- Run full `ctest` suite and verify existing golden/smoke tests pass unchanged.

## 2) Replace ad-hoc run-plan extraction with typed scenario model

**Current hotspots**
- `src/labops/cli/router.cpp` (`LoadRunPlanFromScenario`)
- `src/scenarios/validator.cpp`

**Issue**
- Validation and run loading both traverse JSON, but via separate logic paths.

**Refactor (no behavior change)**
- Add `scenarios/model.hpp/.cpp` with `ScenarioModel` parse result.
- Validator still validates; run planning uses the parsed model instead of ad-hoc reads.
- Keep legacy-key compatibility behavior by encoding that fallback in one parser path.

**Why this helps**
- Prevents drift between what validates and what executes.
- Reduces duplicate path/key handling logic.

**Verification**
- Existing scenario validation and run-path tests pass.
- Add equivalence tests: old run plan vs new run plan for same fixture inputs.

## 3) Introduce a bundle artifact registry object

**Current hotspots**
- `src/labops/cli/router.cpp` (manifest path collection in pause/completion paths)

**Issue**
- Artifact collection is better than before but still assembled from many local variables across branches.

**Refactor (no behavior change)**
- Add `artifacts/bundle_registry.hpp/.cpp`:
  - `RegisterRequired(path)`
  - `RegisterOptional(path)`
  - `RegisterMany(paths)`
  - `BuildManifestInput()`
- Replace local vector assembly with registry calls.

**Why this helps**
- Fewer missed artifacts when new outputs are added.
- Lower branch complexity in soak pause/completion flows.

**Verification**
- Existing bundle layout and manifest smoke tests pass.
- Pause and final paths produce same manifest content as today.

## 4) Move hostprobe platform implementations to dedicated compilation units

**Current hotspot**
- `src/hostprobe/system_probe.cpp`

**Issue**
- One very large file intermixes Linux/macOS/Windows probing and parsing.

**Refactor (no behavior change)**
- Keep public API in `system_probe.hpp` unchanged.
- Split internals into:
  - `system_probe_common.cpp`
  - `system_probe_linux.cpp`
  - `system_probe_macos.cpp`
  - `system_probe_windows.cpp`

**Why this helps**
- Faster local reasoning and safer platform-specific changes.
- Smaller compile units and cleaner conditional compilation boundaries.

**Verification**
- Existing hostprobe/redaction tests pass on CI matrix.
- Output snapshot equivalence on at least one run per platform.

## 5) Extract reconnect/disconnect policy into a dedicated module

**Current hotspots**
- `src/labops/cli/router.cpp` (disconnect detection + reconnect loop)

**Issue**
- Retry policy and stream loop behavior are tightly coupled.

**Refactor (no behavior change)**
- Add `backends/real_sdk/reconnect_policy.hpp/.cpp` that owns:
  - disconnect classification
  - attempt budgeting
  - reconnect attempt execution contract
- Router consumes policy result/events.

**Why this helps**
- Easier to adjust reconnect strategy without touching streaming logic.
- Better testability with mocks for connect/start outcomes.

**Verification**
- Existing disconnect/reconnect smoke tests pass.
- Event and error text snapshots remain stable.

## 6) Standardize common utility helpers across modules

**Current hotspots**
- JSON escaping, timestamp formatting, and file write patterns in multiple modules (`agent`, `events`, `core/schema`, `artifacts`, `hostprobe`).

**Issue**
- Duplicate low-level logic increases subtle inconsistency risk.

**Refactor (no behavior change)**
- Expand `core/json_utils.hpp` and `core/time_utils.hpp` to be the single source for:
  - JSON string escaping
  - UTC timestamp formatting
  - consistent floating-point formatting helpers
- Add shared atomic write helper in `core/fs_utils.hpp` for “write temp + rename”.

**Why this helps**
- Uniform outputs and fewer repeated helper bugs.
- Better reliability for checkpoint and artifact writes.

**Verification**
- Serialization tests unchanged.
- Snapshot comparisons of generated JSON/markdown artifacts remain byte-stable where required.

## 7) Consolidate command-line parsing into declarative option specs

**Current hotspots**
- `ParseRunOptions`, `ParseBaselineCaptureOptions`, `ParseCompareOptions`, etc.

**Issue**
- Manual parser branches are repetitive and easy to drift.

**Refactor (no behavior change)**
- Add a small internal option parser utility with declarative option tables.
- Preserve current option names, validation messages, and error exits.

**Why this helps**
- Easier to add options correctly once.
- Less boilerplate and fewer edge-case differences between commands.

**Verification**
- Existing CLI contract smoke tests pass.
- Add parser parity tests for known invalid/valid argument sets.

## 8) Add a thin event emission facade

**Current hotspots**
- `AppendTraceEvent` callsites in run flow

**Issue**
- Event payload construction is inline and repetitive.

**Refactor (no behavior change)**
- Create `events/emitter.hpp/.cpp` with typed methods:
  - `EmitStreamStarted(...)`
  - `EmitFrameOutcome(...)`
  - `EmitConfigApplied/Unsupported/Adjusted(...)`
  - `EmitTransportAnomaly(...)`
- Internally still write the same event JSONL format.

**Why this helps**
- Event contract remains explicit and less error-prone.
- Easier to keep payload fields consistent.

**Verification**
- Existing event-model tests and stream-trace smokes pass.
- Golden compare of event payload keys for sample runs.

## 9) Strengthen real-parameter application internals with table-driven rules

**Current hotspots**
- `src/backends/real_sdk/apply_params.cpp`

**Issue**
- Logic is improved but still rich in branch-heavy key-specific handling.

**Refactor (no behavior change)**
- Define per-key rule descriptors (type, limits, ordering constraints, transform/readback hooks).
- Keep emitted statuses (`applied`, `adjusted`, `unsupported`) identical.

**Why this helps**
- Easier to add new knobs without branching complexity.
- Better confidence in strict vs best-effort consistency.

**Verification**
- Existing real-backend mock tests pass.
- Add table-driven test cases for each supported key.

## 10) Soak checkpoint robustness polish

**Current hotspots**
- `src/labops/soak/checkpoint_store.cpp`

**Issue**
- Uses ad-hoc JSON field extraction and direct writes.

**Refactor (no behavior change)**
- Parse checkpoint JSON through shared DOM parser.
- Use atomic writes for checkpoint artifacts to avoid partial corruption.
- Keep exact checkpoint schema and fields unchanged.

**Why this helps**
- More robust resume behavior after interruptions.
- Cleaner error diagnostics on malformed checkpoints.

**Verification**
- Existing soak tests pass.
- Add tests for interrupted write simulation and malformed checkpoint recovery.

## 11) Improve test ergonomics with reusable scenario/run fixtures

**Current hotspots**
- repeated test setup across `tests/labops`, `tests/agent`, `tests/scenarios`

**Issue**
- Boilerplate can hide intent and slow test additions.

**Refactor (no behavior change)**
- Add shared helpers for temp roots, run invocation wrappers, fixture scenario generation, and common assertions.
- Keep test behavior and assertions the same.

**Why this helps**
- Faster creation of regression tests.
- Cleaner test files focused on behavior, not setup.

**Verification**
- Test suite runtime and pass results unchanged.

## 12) Introduce architecture-level invariants doc and lightweight checks

**Current opportunity**
- There are implicit invariants (artifact names, exit codes, event fields, threshold semantics).

**Refactor (no behavior change)**
- Add a `docs/architecture_invariants.md` and optionally a small “contract-check” test target.
- Encode invariant checks from existing outputs.

**Why this helps**
- Makes expected behavior explicit for future contributors.
- Prevents accidental contract breaks during refactors.

**Verification**
- Contract checks run in CI with no behavior changes.

## Suggested execution plan

- Phase 1 (low-risk, high leverage): items 6, 3, 11.
- Phase 2 (medium): items 1, 4, 8.
- Phase 3 (higher leverage/risk): items 2, 5, 9, 10.
- Phase 4 (governance): item 12.

## Definition of done for each refactor

- No CLI contract changes.
- No artifact contract changes.
- Existing tests pass.
- Added focused regression tests for touched behavior.
- Clang format + CI green.

