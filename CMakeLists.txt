cmake_minimum_required(VERSION 3.20)

# Root build definition for the LabOps CLI. Keep this minimal early so new
# modules can be added incrementally without fighting build-system complexity.
project(labops_agent VERSION 0.1.0 LANGUAGES CXX)

# Standardize on C++20 and disable compiler extensions so behavior stays
# portable across CI platforms (Linux/macOS/Windows).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Catch2 unit tests are optional locally (offline dev environments) and
# enabled explicitly in CI where dependency fetching is allowed.
option(LABOPS_ENABLE_CATCH2_TESTS "Enable Catch2-based unit tests" ON)
option(LABOPS_FETCH_CATCH2 "Fetch Catch2 when not available locally" OFF)

# Real backend integration path toggle.
# Default stays OFF so Linux/macOS/Windows builds do not require any
# proprietary vendor SDK in local dev or CI.
option(LABOPS_ENABLE_REAL_BACKEND
  "Enable real camera backend integration path (sdk_stub only in this repository)"
  OFF
)

# Vendor SDK discovery inputs. These are intentionally path-only plumbing so
# local integration can be configured without checking proprietary artifacts
# into source control.
set(VENDOR_SDK_ROOT "" CACHE PATH "Vendor SDK root directory (contains include/ and lib/)")
set(VENDOR_SDK_INCLUDE "" CACHE PATH "Vendor SDK include directory override")
set(VENDOR_SDK_LIB "" CACHE PATH "Vendor SDK library directory override")

# Keep custom CMake discovery modules colocated under `cmake/`.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Effective backend state used by compile definitions and future SDK wiring.
# If real backend is requested but SDK paths cannot be resolved, we degrade
# safely to OFF so builds stay portable across environments.
set(LABOPS_REAL_BACKEND_EFFECTIVE OFF)
if(LABOPS_ENABLE_REAL_BACKEND)
  find_package(VendorSDK QUIET)
  if(VendorSDK_FOUND)
    set(LABOPS_REAL_BACKEND_EFFECTIVE ON)
    message(STATUS "Real backend enabled: SDK found")
    message(STATUS "Vendor SDK include dir: ${VendorSDK_INCLUDE_DIR}")
    message(STATUS "Vendor SDK library dir: ${VendorSDK_LIBRARY_DIR}")
  else()
    message(STATUS "SDK missing (real backend disabled)")
  endif()
else()
  message(STATUS "Real backend disabled (LABOPS_ENABLE_REAL_BACKEND=OFF)")
endif()

# Shared schema library for run contracts. Keeping this in a standalone target
# makes it easy for CLI, tests, and future modules to reuse the same contract
# and serialization behavior.
add_library(labops_schema
  src/core/schema/run_contract.cpp
)
target_include_directories(labops_schema PUBLIC src)
target_compile_features(labops_schema PUBLIC cxx_std_20)

# Event model and JSONL writer for timeline evidence emission.
add_library(labops_events
  src/events/emitter.cpp
  src/events/event_model.cpp
  src/events/jsonl_writer.cpp
  src/events/transport_anomaly.cpp
)
target_include_directories(labops_events PUBLIC src)
target_compile_features(labops_events PUBLIC cxx_std_20)

# Camera backend contracts plus sim implementation used for hardware-free
# development and CI.
add_library(labops_backends
  src/backends/real_sdk/acquisition_loop.cpp
  src/backends/real_sdk/apply_params.cpp
  src/backends/real_sdk/error_mapper.cpp
  src/backends/real_sdk/frame_provider.cpp
  src/backends/real_sdk/node_map_adapter.cpp
  src/backends/real_sdk/param_key_map.cpp
  src/backends/real_sdk/real_backend.cpp
  src/backends/real_sdk/real_backend_factory.cpp
  src/backends/real_sdk/reconnect_policy.cpp
  src/backends/real_sdk/sdk_context.cpp
  src/backends/real_sdk/stream_session.cpp
  src/backends/real_sdk/transport_counters.cpp
  src/backends/sdk_stub/real_camera_backend_stub.cpp
  src/backends/sim/scenario_config.cpp
  src/backends/sim/sim_camera_backend.cpp
  src/backends/webcam/capabilities.cpp
  src/backends/webcam/platform_probe.cpp
  src/backends/webcam/webcam_factory.cpp
  src/backends/webcam/linux/platform_probe_linux.cpp
  src/backends/webcam/macos/platform_probe_macos.cpp
  src/backends/webcam/windows/platform_probe_windows.cpp
  src/backends/webcam/webcam_backend.cpp
)
target_include_directories(labops_backends PUBLIC src)
target_compile_features(labops_backends PUBLIC cxx_std_20)
if(LABOPS_ENABLE_REAL_BACKEND)
  target_compile_definitions(labops_backends PUBLIC LABOPS_REAL_BACKEND_REQUESTED=1)
else()
  target_compile_definitions(labops_backends PUBLIC LABOPS_REAL_BACKEND_REQUESTED=0)
endif()
if(LABOPS_REAL_BACKEND_EFFECTIVE)
  target_compile_definitions(labops_backends PUBLIC LABOPS_ENABLE_REAL_BACKEND=1)
  target_include_directories(labops_backends PUBLIC "${VendorSDK_INCLUDE_DIR}")
else()
  target_compile_definitions(labops_backends PUBLIC LABOPS_ENABLE_REAL_BACKEND=0)
endif()

# Scenario loader/validation module.
add_library(labops_scenarios
  src/scenarios/model.cpp
  src/scenarios/netem_profile_support.cpp
  src/scenarios/validator.cpp
)
target_include_directories(labops_scenarios PUBLIC src)
target_compile_features(labops_scenarios PUBLIC cxx_std_20)
# Scenario validation now reuses real-backend selector parsing so validate/run
# stay in lockstep for device selector syntax and error behavior.
target_link_libraries(labops_scenarios PUBLIC labops_backends)

# Host probe collectors for machine context evidence (`hostprobe.json`).
add_library(labops_hostprobe
  src/hostprobe/system_probe_common.cpp
  src/hostprobe/system_probe_linux.cpp
  src/hostprobe/system_probe_macos.cpp
  src/hostprobe/system_probe_windows.cpp
)
target_include_directories(labops_hostprobe PUBLIC src)
target_compile_features(labops_hostprobe PUBLIC cxx_std_20)

# Soak-mode persistence module used by CLI pause/resume orchestration.
add_library(labops_soak
  src/labops/soak/checkpoint_store.cpp
)
target_include_directories(labops_soak PUBLIC src)
target_compile_features(labops_soak PUBLIC cxx_std_20)

# Agent-state contracts for experiment planning checkpoints (`agent_state.json`).
add_library(labops_agent
  src/agent/engineer_packet_writer.cpp
  src/agent/experiment_runner.cpp
  src/agent/experiment_state.cpp
  src/agent/playbook.cpp
  src/agent/state_writer.cpp
  src/agent/stop_conditions.cpp
  src/agent/variant_generator.cpp
)
target_include_directories(labops_agent PUBLIC src)
target_compile_features(labops_agent PUBLIC cxx_std_20)

# Metrics formulas and writers for run performance outputs.
add_library(labops_metrics
  src/metrics/anomalies.cpp
  src/metrics/fps.cpp
)
target_include_directories(labops_metrics PUBLIC src)
target_compile_features(labops_metrics PUBLIC cxx_std_20)

# Artifact writers convert in-memory run contracts into stable on-disk outputs.
add_library(labops_artifacts
  src/artifacts/bundle_registry.cpp
  src/artifacts/bundle_manifest_writer.cpp
  src/artifacts/bundle_zip_writer.cpp
  src/artifacts/camera_config_writer.cpp
  src/artifacts/config_report_writer.cpp
  src/artifacts/config_verify_writer.cpp
  src/artifacts/kb_draft_writer.cpp
  src/artifacts/html_report_writer.cpp
  src/artifacts/hostprobe_writer.cpp
  src/artifacts/metrics_diff_writer.cpp
  src/artifacts/metrics_writer.cpp
  src/artifacts/run_summary_writer.cpp
  src/artifacts/scenario_writer.cpp
  src/artifacts/run_writer.cpp
)
target_include_directories(labops_artifacts PUBLIC src)
target_compile_features(labops_artifacts PUBLIC cxx_std_20)
target_link_libraries(labops_artifacts PUBLIC labops_hostprobe labops_metrics labops_schema)

add_executable(labops
  src/labops/cli/router.cpp
  src/labops/main.cpp
)

# Expose `src/` as the include root so includes stay stable as the tree grows.
target_include_directories(labops PRIVATE src)
target_compile_features(labops PRIVATE cxx_std_20)
target_link_libraries(labops PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema labops_soak)

# Early smoke-test target that validates the schema contract and JSON output
# remain stable. This is intentionally lightweight until broader test
# infrastructure is introduced.
include(CTest)
enable_testing()

# Small helper macro to keep smoke-test declarations concise and consistent.
# It centralizes the common executable -> link -> cxx_std_20 -> add_test flow.
macro(add_labops_smoke_test)
  set(options)
  set(oneValueArgs NAME)
  set(multiValueArgs SOURCES LIBRARIES INCLUDE_DIRS)
  cmake_parse_arguments(LABOPS_SMOKE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT LABOPS_SMOKE_NAME)
    message(FATAL_ERROR "add_labops_smoke_test requires NAME")
  endif()
  if(NOT LABOPS_SMOKE_SOURCES)
    message(FATAL_ERROR "add_labops_smoke_test(${LABOPS_SMOKE_NAME}) requires SOURCES")
  endif()

  add_executable(${LABOPS_SMOKE_NAME}
    ${LABOPS_SMOKE_SOURCES}
  )
  if(LABOPS_SMOKE_INCLUDE_DIRS)
    target_include_directories(${LABOPS_SMOKE_NAME} PRIVATE ${LABOPS_SMOKE_INCLUDE_DIRS})
  endif()
  if(LABOPS_SMOKE_LIBRARIES)
    target_link_libraries(${LABOPS_SMOKE_NAME} PRIVATE ${LABOPS_SMOKE_LIBRARIES})
  endif()
  target_compile_features(${LABOPS_SMOKE_NAME} PRIVATE cxx_std_20)
  add_test(NAME ${LABOPS_SMOKE_NAME} COMMAND ${LABOPS_SMOKE_NAME})
endmacro()

set(LABOPS_CLI_SMOKE_LIBRARIES
  labops_artifacts
  labops_backends
  labops_events
  labops_metrics
  labops_scenarios
  labops_schema
  labops_soak
)

# Early smoke-test target that validates the schema contract and JSON output
# remain stable. This is intentionally lightweight until broader test
# infrastructure is introduced.
add_labops_smoke_test(
  NAME run_contract_json_smoke
  SOURCES
    tests/schema/run_contract_json_smoke.cpp
  LIBRARIES
    labops_schema
)

# Smoke test for artifact emission behavior and output path contracts.
add_labops_smoke_test(
  NAME run_writer_smoke
  SOURCES
    tests/artifacts/run_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_schema
)

# Smoke test for real-config readback verification artifact contract.
add_labops_smoke_test(
  NAME config_verify_writer_smoke
  SOURCES
    tests/artifacts/config_verify_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_schema
)

# Smoke test for real camera-config report artifact contract.
add_labops_smoke_test(
  NAME camera_config_writer_smoke
  SOURCES
    tests/artifacts/camera_config_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_schema
)

# Smoke test for human-readable config report artifact contract.
add_labops_smoke_test(
  NAME config_report_writer_smoke
  SOURCES
    tests/artifacts/config_report_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_schema
)

# Smoke test for agent experiment-state serialization and artifact writing.
add_labops_smoke_test(
  NAME agent_state_writer_smoke
  SOURCES
    tests/agent/agent_state_writer_smoke.cpp
  LIBRARIES
    labops_agent
)

# Smoke test for in-process agent experiment execution:
# baseline capture + one variant run without shelling out.
add_labops_smoke_test(
  NAME experiment_runner_smoke
  SOURCES
    tests/agent/experiment_runner_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    labops_agent
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Smoke test for clean failure behavior when experiment input paths are invalid.
add_labops_smoke_test(
  NAME experiment_runner_failure_smoke
  SOURCES
    tests/agent/experiment_runner_failure_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    labops_agent
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Smoke test for symptom-driven playbook selection and ordered knob output.
add_labops_smoke_test(
  NAME playbook_selection_smoke
  SOURCES
    tests/agent/playbook_selection_smoke.cpp
  LIBRARIES
    labops_agent
)

# Smoke test for one-variable-at-a-time variant generation.
add_labops_smoke_test(
  NAME oaat_variant_generator_smoke
  SOURCES
    tests/agent/oaat_variant_generator_smoke.cpp
  LIBRARIES
    labops_agent
)

# Smoke test for deterministic stop-condition evaluation and explanation.
add_labops_smoke_test(
  NAME stop_conditions_smoke
  SOURCES
    tests/agent/stop_conditions_smoke.cpp
  LIBRARIES
    labops_agent
)

# Smoke test for engineer packet generation with exact artifact and diff links.
add_labops_smoke_test(
  NAME engineer_packet_writer_smoke
  SOURCES
    tests/agent/engineer_packet_writer_smoke.cpp
  LIBRARIES
    labops_agent
)

# Integration smoke test for seeded agent triage on a simulated failure path.
# Exercises OAAT generation -> run/compare evidence -> stop decision ->
# engineer packet emission in one deterministic flow.
add_labops_smoke_test(
  NAME agent_triage_integration_smoke
  SOURCES
    tests/agent/agent_triage_integration_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    labops_agent
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Smoke test for scenario snapshot writer contract.
add_labops_smoke_test(
  NAME scenario_writer_smoke
  SOURCES
    tests/artifacts/scenario_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
)

# Smoke test for bundle manifest writer contract.
add_labops_smoke_test(
  NAME bundle_manifest_writer_smoke
  SOURCES
    tests/artifacts/bundle_manifest_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
)

# Smoke test for bundle artifact registry behavior (required vs optional).
add_labops_smoke_test(
  NAME bundle_registry_smoke
  SOURCES
    tests/artifacts/bundle_registry_smoke.cpp
  LIBRARIES
    labops_artifacts
)

# Smoke test for support-bundle zip writer contract.
add_labops_smoke_test(
  NAME bundle_zip_writer_smoke
  SOURCES
    tests/artifacts/bundle_zip_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
)

# Smoke test for append-only event log emission.
add_labops_smoke_test(
  NAME events_jsonl_smoke
  SOURCES
    tests/events/events_jsonl_smoke.cpp
  LIBRARIES
    labops_events
)

# Smoke test for typed event emitter facade payload contracts.
add_labops_smoke_test(
  NAME emitter_smoke
  SOURCES
    tests/events/emitter_smoke.cpp
  LIBRARIES
    labops_events
)

# Smoke test for transport-counter anomaly heuristics.
add_labops_smoke_test(
  NAME transport_anomaly_smoke
  SOURCES
    tests/events/transport_anomaly_smoke.cpp
  LIBRARIES
    labops_events
)

# Smoke test validating that sim backend satisfies ICameraBackend contract.
add_labops_smoke_test(
  NAME sim_backend_interface_smoke
  SOURCES
    tests/backends/sim_backend_interface_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test validating sdk_stub compiles without proprietary SDK dependencies
# and reports actionable "not implemented" behavior.
add_labops_smoke_test(
  NAME sdk_stub_backend_smoke
  SOURCES
    tests/backends/sdk_stub_backend_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test validating webcam backend scaffolding:
# compile-stable module with explicit BACKEND_NOT_AVAILABLE behavior until
# per-platform capture implementations are integrated.
add_labops_smoke_test(
  NAME webcam_backend_smoke
  SOURCES
    tests/backends/webcam_backend_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for real-backend factory behavior:
# enabled builds select the real skeleton, disabled builds fall back to sdk_stub.
add_labops_smoke_test(
  NAME real_backend_factory_smoke
  SOURCES
    tests/backends/real_backend_factory_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for process-level SDK context RAII behavior:
# init once across multiple handles and safe shutdown on final release.
add_labops_smoke_test(
  NAME sdk_context_smoke
  SOURCES
    tests/backends/sdk_context_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for real-backend stream-session lifecycle:
# safe start/stop flow and idempotent stop semantics.
add_labops_smoke_test(
  NAME real_stream_session_smoke
  SOURCES
    tests/backends/real_stream_session_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for real-backend single-thread frame acquisition:
# deterministic per-frame outcomes with metrics-ready samples.
add_labops_smoke_test(
  NAME real_frame_acquisition_smoke
  SOURCES
    tests/backends/real_frame_acquisition_smoke.cpp
  LIBRARIES
    labops_backends
    labops_metrics
)

# Smoke test for real acquisition-loop behavior driven by a mock frame provider
# (timeouts/incomplete outcomes plus burst stalls) without any SDK dependency.
add_labops_smoke_test(
  NAME real_acquisition_loop_mock_provider_smoke
  SOURCES
    tests/backends/real_acquisition_loop_mock_provider_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for real device enumeration and descriptor normalization.
add_labops_smoke_test(
  NAME real_device_enumeration_smoke
  SOURCES
    tests/backends/real_device_enumeration_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for selector parsing/resolution against discovered real devices.
add_labops_smoke_test(
  NAME real_device_selector_resolution_smoke
  SOURCES
    tests/backends/real_device_selector_resolution_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for generic-key node-map adapter query/set contracts.
add_labops_smoke_test(
  NAME node_map_adapter_smoke
  SOURCES
    tests/backends/node_map_adapter_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for apply-logic coverage using a mock node-map adapter (no hardware).
add_labops_smoke_test(
  NAME mock_node_map_adapter_smoke
  SOURCES
    tests/backends/mock_node_map_adapter_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for data-driven generic-key -> SDK-node map loading.
add_labops_smoke_test(
  NAME param_key_map_smoke
  SOURCES
    tests/backends/param_key_map_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for strict vs best-effort real-backend parameter application.
add_labops_smoke_test(
  NAME real_apply_params_smoke
  SOURCES
    tests/backends/real_apply_params_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for table-driven per-key apply-rule coverage across all supported
# generic->node mappings.
add_labops_smoke_test(
  NAME real_apply_params_rule_table_smoke
  SOURCES
    tests/backends/real_apply_params_rule_table_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for best-effort transport counter collection alias handling.
add_labops_smoke_test(
  NAME real_transport_counters_smoke
  SOURCES
    tests/backends/real_transport_counters_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for stable/actionable real-backend error classification.
add_labops_smoke_test(
  NAME real_error_mapper_smoke
  SOURCES
    tests/backends/real_error_mapper_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for disconnect classification and reconnect-attempt policy flow.
add_labops_smoke_test(
  NAME real_reconnect_policy_smoke
  SOURCES
    tests/backends/real_reconnect_policy_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for deterministic frame generation and timing envelope.
add_labops_smoke_test(
  NAME sim_frame_generator_smoke
  SOURCES
    tests/backends/sim_frame_generator_smoke.cpp
  LIBRARIES
    labops_backends
)

# Smoke test for scenario-driven fault injection reproducibility.
add_labops_smoke_test(
  NAME sim_fault_injection_smoke
  SOURCES
    tests/backends/sim_fault_injection_smoke.cpp
  LIBRARIES
    labops_backends
)

# Integration smoke test that drives `labops run` and validates stream events.
add_labops_smoke_test(
  NAME run_stream_trace_smoke
  SOURCES
    tests/labops/run_stream_trace_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Architecture contract smoke test:
# validates stable artifact names, key event payload fields, and representative
# exit-code semantics (success / thresholds-failed / schema-invalid).
add_labops_smoke_test(
  NAME architecture_contract_check_smoke
  SOURCES
    tests/labops/architecture_contract_check_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test validating Ctrl+C shutdown behavior:
# interrupted runs still flush a complete artifact bundle before exit.
add_labops_smoke_test(
  NAME run_interrupt_flush_smoke
  SOURCES
    tests/labops/run_interrupt_flush_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for optional SDK log capture:
# `--sdk-log` should emit `sdk_log.txt` without changing run result behavior.
add_labops_smoke_test(
  NAME sdk_log_capture_smoke
  SOURCES
    tests/labops/sdk_log_capture_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test validating reconnect policy for mid-stream device loss:
# emit DEVICE_DISCONNECTED, retry with bounded budget, and still flush a full
# failure bundle when retries are exhausted.
add_labops_smoke_test(
  NAME run_reconnect_policy_smoke
  SOURCES
    tests/labops/run_reconnect_policy_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for actionable schema validation errors through CLI.
add_labops_smoke_test(
  NAME validate_actionable_smoke
  SOURCES
    tests/labops/validate_actionable_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test that verifies deterministic stream traces for a fixed
# scenario seed (same seed => same first K events after normalizing dynamic fields).
add_labops_smoke_test(
  NAME sim_determinism_golden_smoke
  SOURCES
    tests/labops/sim_determinism_golden_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test that runs starter scenarios end-to-end through the
# CLI and validates core artifact/event outputs for each scenario.
add_labops_smoke_test(
  NAME starter_scenarios_e2e_smoke
  SOURCES
    tests/labops/starter_scenarios_e2e_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test validating standardized bundle layout:
# `<out>/<run_id>/...` with required artifact set for each run.
add_labops_smoke_test(
  NAME bundle_layout_consistency_smoke
  SOURCES
    tests/labops/bundle_layout_consistency_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test validating `--zip` behavior:
# zip is generated only when explicitly requested.
add_labops_smoke_test(
  NAME bundle_zip_on_demand_smoke
  SOURCES
    tests/labops/bundle_zip_on_demand_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for `labops baseline capture` contract:
# writes baseline artifacts directly to `baselines/<scenario_id>/` with metrics.
add_labops_smoke_test(
  NAME baseline_capture_smoke
  SOURCES
    tests/labops/baseline_capture_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for `labops compare`:
# compares baseline vs run metrics and emits `diff.json` + `diff.md`.
add_labops_smoke_test(
  NAME compare_diff_smoke
  SOURCES
    tests/labops/compare_diff_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for scenario threshold enforcement:
# `labops run` must return non-zero when configured thresholds fail.
add_labops_smoke_test(
  NAME run_threshold_failure_smoke
  SOURCES
    tests/labops/run_threshold_failure_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for backend connection failure exit-code contract:
# selecting `backend: real_stub` should deterministically fail connect and map
# to the dedicated backend-connect exit code.
add_labops_smoke_test(
  NAME run_backend_connect_failure_smoke
  SOURCES
    tests/labops/run_backend_connect_failure_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for netem CLI option contracts.
# Ensures netem execution flags require explicit pairing:
# - `--apply-netem` requires `--netem-iface <iface>`
# - `--netem-iface` requires `--apply-netem`
add_labops_smoke_test(
  NAME netem_option_contract_smoke
  SOURCES
    tests/labops/netem_option_contract_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test validating structured logging behavior:
# - `--log-level` controls emitted verbosity
# - log lines include run_id for artifact correlation
add_labops_smoke_test(
  NAME logging_contract_smoke
  SOURCES
    tests/labops/logging_contract_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for backend availability listing contract.
add_labops_smoke_test(
  NAME list_backends_smoke
  SOURCES
    tests/labops/list_backends_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for run-time selector resolution via:
# `labops run ... --device <selector>`
add_labops_smoke_test(
  NAME run_device_selector_resolution_smoke
  SOURCES
    tests/labops/run_device_selector_resolution_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for real-backend device listing contract.
add_labops_smoke_test(
  NAME list_devices_real_backend_smoke
  SOURCES
    tests/labops/list_devices_real_backend_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for strict vs best-effort real apply-mode event flow.
add_labops_smoke_test(
  NAME real_apply_mode_events_smoke
  SOURCES
    tests/labops/real_apply_mode_events_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for KB draft generation from a run folder.
add_labops_smoke_test(
  NAME kb_draft_from_run_folder_smoke
  SOURCES
    tests/labops/kb_draft_from_run_folder_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Integration smoke test for checkpointed soak mode:
# - periodic checkpoint emission
# - safe pause at checkpoint boundary
# - resume from checkpoint without evidence loss
add_labops_smoke_test(
  NAME soak_checkpoint_resume_smoke
  SOURCES
    tests/labops/soak_checkpoint_resume_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Module-level smoke test for checkpoint/frame-cache persistence primitives.
add_labops_smoke_test(
  NAME soak_checkpoint_store_smoke
  SOURCES
    tests/labops/soak_checkpoint_store_smoke.cpp
  LIBRARIES
    labops_soak
)

# Module-level smoke test for checkpoint robustness:
# - interrupted atomic-write simulation keeps existing checkpoint intact
# - malformed checkpoint parsing fails clearly and recovers after rewrite.
add_labops_smoke_test(
  NAME soak_checkpoint_resilience_smoke
  SOURCES
    tests/labops/soak_checkpoint_resilience_smoke.cpp
  LIBRARIES
    labops_soak
)

# Scenario schema validation smoke tests.
add_labops_smoke_test(
  NAME scenario_validation_smoke
  SOURCES
    tests/scenarios/scenario_validation_smoke.cpp
  LIBRARIES
    labops_scenarios
)

# Scenario runtime-model parser smoke tests, including canonical/legacy-key
# equivalence for run-planning inputs.
add_labops_smoke_test(
  NAME scenario_model_equivalence_smoke
  SOURCES
    tests/scenarios/scenario_model_equivalence_smoke.cpp
  LIBRARIES
    labops_scenarios
)

# Integration smoke test that runs the checked-in baseline scenario through
# `labops run` and asserts output metrics remain within expected ranges.
add_labops_smoke_test(
  NAME sim_baseline_metrics_integration_smoke
  SOURCES
    tests/scenarios/sim_baseline_metrics_integration_smoke.cpp
    src/labops/cli/router.cpp
  INCLUDE_DIRS
    src
  LIBRARIES
    ${LABOPS_CLI_SMOKE_LIBRARIES}
)

# Smoke test for FPS computation and metrics.csv emission.
add_labops_smoke_test(
  NAME fps_metrics_smoke
  SOURCES
    tests/metrics/fps_metrics_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_metrics
)

# Artifact smoke test for metrics.csv + metrics.json writer contracts.
add_labops_smoke_test(
  NAME metrics_writers_smoke
  SOURCES
    tests/artifacts/metrics_writers_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_metrics
)

# Artifact smoke test for static HTML report writer contract.
add_labops_smoke_test(
  NAME html_report_writer_smoke
  SOURCES
    tests/artifacts/html_report_writer_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_metrics
    labops_schema
)

# Smoke test validating that timing/jitter metrics reflect injected jitter.
add_labops_smoke_test(
  NAME jitter_injection_smoke
  SOURCES
    tests/metrics/jitter_injection_smoke.cpp
  LIBRARIES
    labops_backends
    labops_metrics
)

# Smoke test validating drop metrics match deterministic drop injection knobs.
add_labops_smoke_test(
  NAME drop_injection_smoke
  SOURCES
    tests/metrics/drop_injection_smoke.cpp
  LIBRARIES
    labops_artifacts
    labops_backends
    labops_metrics
)

# Smoke test for named anomaly heuristics in run-summary highlights.
add_labops_smoke_test(
  NAME anomaly_detection_smoke
  SOURCES
    tests/metrics/anomaly_detection_smoke.cpp
  LIBRARIES
    labops_metrics
)

# Smoke test validating that host/user identifiers are redacted from host probe
# JSON and raw NIC command outputs when redaction is applied.
add_labops_smoke_test(
  NAME hostprobe_redaction_smoke
  SOURCES
    tests/hostprobe/redaction_smoke.cpp
  LIBRARIES
    labops_hostprobe
)

# Manual (non-CTEST) real-camera smoke target for lab bring-up.
# This is intentionally a custom target instead of `add_test(...)` so CI does
# not require cameras or SDK-connected hardware.
#
# Flow:
# - connect
# - stream for 5 seconds
# - emit normal run artifacts (including config dump)
# - exit
add_custom_target(real_camera_smoke_manual
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/tmp/manual_real_camera_smoke"
  COMMAND
    ${CMAKE_COMMAND} -E chdir "${CMAKE_SOURCE_DIR}"
    "$<TARGET_FILE:labops>"
    run
    "tests/manual/real_camera_smoke_scenario.json"
    --out
    "tmp/manual_real_camera_smoke"
  DEPENDS labops
  USES_TERMINAL
  COMMENT "Manual real camera smoke run (connect -> stream 5s -> dump config -> exit)"
)

if(LABOPS_ENABLE_CATCH2_TESTS)
  find_package(Catch2 3 QUIET)

  if((NOT Catch2_FOUND) AND LABOPS_FETCH_CATCH2)
    include(FetchContent)
    FetchContent_Declare(
      Catch2
      URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    find_package(Catch2 3 QUIET)
  endif()

  if(Catch2_FOUND)
    add_executable(core_unit_tests
      tests/core/event_json_test.cpp
      tests/core/schema_json_test.cpp
    )
    target_link_libraries(core_unit_tests PRIVATE Catch2::Catch2WithMain labops_events labops_schema)
    target_compile_features(core_unit_tests PRIVATE cxx_std_20)

    include(Catch)
    catch_discover_tests(core_unit_tests)
  else()
    message(WARNING "Catch2 not found. Skipping core_unit_tests. Set LABOPS_FETCH_CATCH2=ON to fetch it.")
  endif()
endif()
