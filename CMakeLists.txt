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
  src/events/event_model.cpp
  src/events/jsonl_writer.cpp
)
target_include_directories(labops_events PUBLIC src)
target_compile_features(labops_events PUBLIC cxx_std_20)

# Camera backend contracts plus sim implementation used for hardware-free
# development and CI.
add_library(labops_backends
  src/backends/real_sdk/real_backend.cpp
  src/backends/real_sdk/real_backend_factory.cpp
  src/backends/real_sdk/sdk_context.cpp
  src/backends/sdk_stub/real_camera_backend_stub.cpp
  src/backends/sim/scenario_config.cpp
  src/backends/sim/sim_camera_backend.cpp
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
  src/scenarios/validator.cpp
)
target_include_directories(labops_scenarios PUBLIC src)
target_compile_features(labops_scenarios PUBLIC cxx_std_20)

# Host probe collectors for machine context evidence (`hostprobe.json`).
add_library(labops_hostprobe
  src/hostprobe/system_probe.cpp
)
target_include_directories(labops_hostprobe PUBLIC src)
target_compile_features(labops_hostprobe PUBLIC cxx_std_20)

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
  src/artifacts/bundle_manifest_writer.cpp
  src/artifacts/bundle_zip_writer.cpp
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
target_link_libraries(labops PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)

# Early smoke-test target that validates the schema contract and JSON output
# remain stable. This is intentionally lightweight until broader test
# infrastructure is introduced.
include(CTest)
enable_testing()
add_executable(run_contract_json_smoke
  tests/schema/run_contract_json_smoke.cpp
)
target_link_libraries(run_contract_json_smoke PRIVATE labops_schema)
target_compile_features(run_contract_json_smoke PRIVATE cxx_std_20)
add_test(NAME run_contract_json_smoke COMMAND run_contract_json_smoke)

# Smoke test for artifact emission behavior and output path contracts.
add_executable(run_writer_smoke
  tests/artifacts/run_writer_smoke.cpp
)
target_link_libraries(run_writer_smoke PRIVATE labops_artifacts labops_schema)
target_compile_features(run_writer_smoke PRIVATE cxx_std_20)
add_test(NAME run_writer_smoke COMMAND run_writer_smoke)

# Smoke test for agent experiment-state serialization and artifact writing.
add_executable(agent_state_writer_smoke
  tests/agent/agent_state_writer_smoke.cpp
)
target_link_libraries(agent_state_writer_smoke PRIVATE labops_agent)
target_compile_features(agent_state_writer_smoke PRIVATE cxx_std_20)
add_test(NAME agent_state_writer_smoke COMMAND agent_state_writer_smoke)

# Smoke test for in-process agent experiment execution:
# baseline capture + one variant run without shelling out.
add_executable(experiment_runner_smoke
  tests/agent/experiment_runner_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(experiment_runner_smoke PRIVATE src)
target_link_libraries(experiment_runner_smoke PRIVATE labops_agent labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(experiment_runner_smoke PRIVATE cxx_std_20)
add_test(NAME experiment_runner_smoke COMMAND experiment_runner_smoke)

# Smoke test for clean failure behavior when experiment input paths are invalid.
add_executable(experiment_runner_failure_smoke
  tests/agent/experiment_runner_failure_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(experiment_runner_failure_smoke PRIVATE src)
target_link_libraries(experiment_runner_failure_smoke PRIVATE labops_agent labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(experiment_runner_failure_smoke PRIVATE cxx_std_20)
add_test(NAME experiment_runner_failure_smoke COMMAND experiment_runner_failure_smoke)

# Smoke test for symptom-driven playbook selection and ordered knob output.
add_executable(playbook_selection_smoke
  tests/agent/playbook_selection_smoke.cpp
)
target_link_libraries(playbook_selection_smoke PRIVATE labops_agent)
target_compile_features(playbook_selection_smoke PRIVATE cxx_std_20)
add_test(NAME playbook_selection_smoke COMMAND playbook_selection_smoke)

# Smoke test for one-variable-at-a-time variant generation.
add_executable(oaat_variant_generator_smoke
  tests/agent/oaat_variant_generator_smoke.cpp
)
target_link_libraries(oaat_variant_generator_smoke PRIVATE labops_agent)
target_compile_features(oaat_variant_generator_smoke PRIVATE cxx_std_20)
add_test(NAME oaat_variant_generator_smoke COMMAND oaat_variant_generator_smoke)

# Smoke test for deterministic stop-condition evaluation and explanation.
add_executable(stop_conditions_smoke
  tests/agent/stop_conditions_smoke.cpp
)
target_link_libraries(stop_conditions_smoke PRIVATE labops_agent)
target_compile_features(stop_conditions_smoke PRIVATE cxx_std_20)
add_test(NAME stop_conditions_smoke COMMAND stop_conditions_smoke)

# Smoke test for engineer packet generation with exact artifact and diff links.
add_executable(engineer_packet_writer_smoke
  tests/agent/engineer_packet_writer_smoke.cpp
)
target_link_libraries(engineer_packet_writer_smoke PRIVATE labops_agent)
target_compile_features(engineer_packet_writer_smoke PRIVATE cxx_std_20)
add_test(NAME engineer_packet_writer_smoke COMMAND engineer_packet_writer_smoke)

# Integration smoke test for seeded agent triage on a simulated failure path.
# Exercises OAAT generation -> run/compare evidence -> stop decision ->
# engineer packet emission in one deterministic flow.
add_executable(agent_triage_integration_smoke
  tests/agent/agent_triage_integration_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(agent_triage_integration_smoke PRIVATE src)
target_link_libraries(agent_triage_integration_smoke PRIVATE labops_agent labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(agent_triage_integration_smoke PRIVATE cxx_std_20)
add_test(NAME agent_triage_integration_smoke COMMAND agent_triage_integration_smoke)

# Smoke test for scenario snapshot writer contract.
add_executable(scenario_writer_smoke
  tests/artifacts/scenario_writer_smoke.cpp
)
target_link_libraries(scenario_writer_smoke PRIVATE labops_artifacts)
target_compile_features(scenario_writer_smoke PRIVATE cxx_std_20)
add_test(NAME scenario_writer_smoke COMMAND scenario_writer_smoke)

# Smoke test for bundle manifest writer contract.
add_executable(bundle_manifest_writer_smoke
  tests/artifacts/bundle_manifest_writer_smoke.cpp
)
target_link_libraries(bundle_manifest_writer_smoke PRIVATE labops_artifacts)
target_compile_features(bundle_manifest_writer_smoke PRIVATE cxx_std_20)
add_test(NAME bundle_manifest_writer_smoke COMMAND bundle_manifest_writer_smoke)

# Smoke test for support-bundle zip writer contract.
add_executable(bundle_zip_writer_smoke
  tests/artifacts/bundle_zip_writer_smoke.cpp
)
target_link_libraries(bundle_zip_writer_smoke PRIVATE labops_artifacts)
target_compile_features(bundle_zip_writer_smoke PRIVATE cxx_std_20)
add_test(NAME bundle_zip_writer_smoke COMMAND bundle_zip_writer_smoke)

# Smoke test for append-only event log emission.
add_executable(events_jsonl_smoke
  tests/events/events_jsonl_smoke.cpp
)
target_link_libraries(events_jsonl_smoke PRIVATE labops_events)
target_compile_features(events_jsonl_smoke PRIVATE cxx_std_20)
add_test(NAME events_jsonl_smoke COMMAND events_jsonl_smoke)

# Smoke test validating that sim backend satisfies ICameraBackend contract.
add_executable(sim_backend_interface_smoke
  tests/backends/sim_backend_interface_smoke.cpp
)
target_link_libraries(sim_backend_interface_smoke PRIVATE labops_backends)
target_compile_features(sim_backend_interface_smoke PRIVATE cxx_std_20)
add_test(NAME sim_backend_interface_smoke COMMAND sim_backend_interface_smoke)

# Smoke test validating sdk_stub compiles without proprietary SDK dependencies
# and reports actionable "not implemented" behavior.
add_executable(sdk_stub_backend_smoke
  tests/backends/sdk_stub_backend_smoke.cpp
)
target_link_libraries(sdk_stub_backend_smoke PRIVATE labops_backends)
target_compile_features(sdk_stub_backend_smoke PRIVATE cxx_std_20)
add_test(NAME sdk_stub_backend_smoke COMMAND sdk_stub_backend_smoke)

# Smoke test for real-backend factory behavior:
# enabled builds select the real skeleton, disabled builds fall back to sdk_stub.
add_executable(real_backend_factory_smoke
  tests/backends/real_backend_factory_smoke.cpp
)
target_link_libraries(real_backend_factory_smoke PRIVATE labops_backends)
target_compile_features(real_backend_factory_smoke PRIVATE cxx_std_20)
add_test(NAME real_backend_factory_smoke COMMAND real_backend_factory_smoke)

# Smoke test for process-level SDK context RAII behavior:
# init once across multiple handles and safe shutdown on final release.
add_executable(sdk_context_smoke
  tests/backends/sdk_context_smoke.cpp
)
target_link_libraries(sdk_context_smoke PRIVATE labops_backends)
target_compile_features(sdk_context_smoke PRIVATE cxx_std_20)
add_test(NAME sdk_context_smoke COMMAND sdk_context_smoke)

# Smoke test for real device enumeration and descriptor normalization.
add_executable(real_device_enumeration_smoke
  tests/backends/real_device_enumeration_smoke.cpp
)
target_link_libraries(real_device_enumeration_smoke PRIVATE labops_backends)
target_compile_features(real_device_enumeration_smoke PRIVATE cxx_std_20)
add_test(NAME real_device_enumeration_smoke COMMAND real_device_enumeration_smoke)

# Smoke test for selector parsing/resolution against discovered real devices.
add_executable(real_device_selector_resolution_smoke
  tests/backends/real_device_selector_resolution_smoke.cpp
)
target_link_libraries(real_device_selector_resolution_smoke PRIVATE labops_backends)
target_compile_features(real_device_selector_resolution_smoke PRIVATE cxx_std_20)
add_test(NAME real_device_selector_resolution_smoke COMMAND real_device_selector_resolution_smoke)

# Smoke test for deterministic frame generation and timing envelope.
add_executable(sim_frame_generator_smoke
  tests/backends/sim_frame_generator_smoke.cpp
)
target_link_libraries(sim_frame_generator_smoke PRIVATE labops_backends)
target_compile_features(sim_frame_generator_smoke PRIVATE cxx_std_20)
add_test(NAME sim_frame_generator_smoke COMMAND sim_frame_generator_smoke)

# Smoke test for scenario-driven fault injection reproducibility.
add_executable(sim_fault_injection_smoke
  tests/backends/sim_fault_injection_smoke.cpp
)
target_link_libraries(sim_fault_injection_smoke PRIVATE labops_backends)
target_compile_features(sim_fault_injection_smoke PRIVATE cxx_std_20)
add_test(NAME sim_fault_injection_smoke COMMAND sim_fault_injection_smoke)

# Integration smoke test that drives `labops run` and validates stream events.
add_executable(run_stream_trace_smoke
  tests/labops/run_stream_trace_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(run_stream_trace_smoke PRIVATE src)
target_link_libraries(run_stream_trace_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(run_stream_trace_smoke PRIVATE cxx_std_20)
add_test(NAME run_stream_trace_smoke COMMAND run_stream_trace_smoke)

# Integration smoke test for actionable schema validation errors through CLI.
add_executable(validate_actionable_smoke
  tests/labops/validate_actionable_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(validate_actionable_smoke PRIVATE src)
target_link_libraries(validate_actionable_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(validate_actionable_smoke PRIVATE cxx_std_20)
add_test(NAME validate_actionable_smoke COMMAND validate_actionable_smoke)

# Integration smoke test that verifies deterministic stream traces for a fixed
# scenario seed (same seed => same first K events after normalizing dynamic fields).
add_executable(sim_determinism_golden_smoke
  tests/labops/sim_determinism_golden_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(sim_determinism_golden_smoke PRIVATE src)
target_link_libraries(sim_determinism_golden_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(sim_determinism_golden_smoke PRIVATE cxx_std_20)
add_test(NAME sim_determinism_golden_smoke COMMAND sim_determinism_golden_smoke)

# Integration smoke test that runs starter scenarios end-to-end through the
# CLI and validates core artifact/event outputs for each scenario.
add_executable(starter_scenarios_e2e_smoke
  tests/labops/starter_scenarios_e2e_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(starter_scenarios_e2e_smoke PRIVATE src)
target_link_libraries(starter_scenarios_e2e_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(starter_scenarios_e2e_smoke PRIVATE cxx_std_20)
add_test(NAME starter_scenarios_e2e_smoke COMMAND starter_scenarios_e2e_smoke)

# Integration smoke test validating standardized bundle layout:
# `<out>/<run_id>/...` with required artifact set for each run.
add_executable(bundle_layout_consistency_smoke
  tests/labops/bundle_layout_consistency_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(bundle_layout_consistency_smoke PRIVATE src)
target_link_libraries(bundle_layout_consistency_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(bundle_layout_consistency_smoke PRIVATE cxx_std_20)
add_test(NAME bundle_layout_consistency_smoke COMMAND bundle_layout_consistency_smoke)

# Integration smoke test validating `--zip` behavior:
# zip is generated only when explicitly requested.
add_executable(bundle_zip_on_demand_smoke
  tests/labops/bundle_zip_on_demand_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(bundle_zip_on_demand_smoke PRIVATE src)
target_link_libraries(bundle_zip_on_demand_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(bundle_zip_on_demand_smoke PRIVATE cxx_std_20)
add_test(NAME bundle_zip_on_demand_smoke COMMAND bundle_zip_on_demand_smoke)

# Integration smoke test for `labops baseline capture` contract:
# writes baseline artifacts directly to `baselines/<scenario_id>/` with metrics.
add_executable(baseline_capture_smoke
  tests/labops/baseline_capture_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(baseline_capture_smoke PRIVATE src)
target_link_libraries(baseline_capture_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(baseline_capture_smoke PRIVATE cxx_std_20)
add_test(NAME baseline_capture_smoke COMMAND baseline_capture_smoke)

# Integration smoke test for `labops compare`:
# compares baseline vs run metrics and emits `diff.json` + `diff.md`.
add_executable(compare_diff_smoke
  tests/labops/compare_diff_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(compare_diff_smoke PRIVATE src)
target_link_libraries(compare_diff_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(compare_diff_smoke PRIVATE cxx_std_20)
add_test(NAME compare_diff_smoke COMMAND compare_diff_smoke)

# Integration smoke test for scenario threshold enforcement:
# `labops run` must return non-zero when configured thresholds fail.
add_executable(run_threshold_failure_smoke
  tests/labops/run_threshold_failure_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(run_threshold_failure_smoke PRIVATE src)
target_link_libraries(run_threshold_failure_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(run_threshold_failure_smoke PRIVATE cxx_std_20)
add_test(NAME run_threshold_failure_smoke COMMAND run_threshold_failure_smoke)

# Integration smoke test for backend connection failure exit-code contract:
# selecting `backend: real_stub` should deterministically fail connect and map
# to the dedicated backend-connect exit code.
add_executable(run_backend_connect_failure_smoke
  tests/labops/run_backend_connect_failure_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(run_backend_connect_failure_smoke PRIVATE src)
target_link_libraries(run_backend_connect_failure_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(run_backend_connect_failure_smoke PRIVATE cxx_std_20)
add_test(NAME run_backend_connect_failure_smoke COMMAND run_backend_connect_failure_smoke)

# Integration smoke test for netem CLI option contracts.
# Ensures netem execution flags require explicit pairing:
# - `--apply-netem` requires `--netem-iface <iface>`
# - `--netem-iface` requires `--apply-netem`
add_executable(netem_option_contract_smoke
  tests/labops/netem_option_contract_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(netem_option_contract_smoke PRIVATE src)
target_link_libraries(netem_option_contract_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(netem_option_contract_smoke PRIVATE cxx_std_20)
add_test(NAME netem_option_contract_smoke COMMAND netem_option_contract_smoke)

# Integration smoke test validating structured logging behavior:
# - `--log-level` controls emitted verbosity
# - log lines include run_id for artifact correlation
add_executable(logging_contract_smoke
  tests/labops/logging_contract_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(logging_contract_smoke PRIVATE src)
target_link_libraries(logging_contract_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(logging_contract_smoke PRIVATE cxx_std_20)
add_test(NAME logging_contract_smoke COMMAND logging_contract_smoke)

# Integration smoke test for backend availability listing contract.
add_executable(list_backends_smoke
  tests/labops/list_backends_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(list_backends_smoke PRIVATE src)
target_link_libraries(list_backends_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(list_backends_smoke PRIVATE cxx_std_20)
add_test(NAME list_backends_smoke COMMAND list_backends_smoke)

# Integration smoke test for run-time selector resolution via:
# `labops run ... --device <selector>`
add_executable(run_device_selector_resolution_smoke
  tests/labops/run_device_selector_resolution_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(run_device_selector_resolution_smoke PRIVATE src)
target_link_libraries(run_device_selector_resolution_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(run_device_selector_resolution_smoke PRIVATE cxx_std_20)
add_test(NAME run_device_selector_resolution_smoke COMMAND run_device_selector_resolution_smoke)

# Integration smoke test for real-backend device listing contract.
add_executable(list_devices_real_backend_smoke
  tests/labops/list_devices_real_backend_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(list_devices_real_backend_smoke PRIVATE src)
target_link_libraries(list_devices_real_backend_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(list_devices_real_backend_smoke PRIVATE cxx_std_20)
add_test(NAME list_devices_real_backend_smoke COMMAND list_devices_real_backend_smoke)

# Integration smoke test for KB draft generation from a run folder.
add_executable(kb_draft_from_run_folder_smoke
  tests/labops/kb_draft_from_run_folder_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(kb_draft_from_run_folder_smoke PRIVATE src)
target_link_libraries(kb_draft_from_run_folder_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(kb_draft_from_run_folder_smoke PRIVATE cxx_std_20)
add_test(NAME kb_draft_from_run_folder_smoke COMMAND kb_draft_from_run_folder_smoke)

# Integration smoke test for checkpointed soak mode:
# - periodic checkpoint emission
# - safe pause at checkpoint boundary
# - resume from checkpoint without evidence loss
add_executable(soak_checkpoint_resume_smoke
  tests/labops/soak_checkpoint_resume_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(soak_checkpoint_resume_smoke PRIVATE src)
target_link_libraries(soak_checkpoint_resume_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(soak_checkpoint_resume_smoke PRIVATE cxx_std_20)
add_test(NAME soak_checkpoint_resume_smoke COMMAND soak_checkpoint_resume_smoke)

# Scenario schema validation smoke tests.
add_executable(scenario_validation_smoke
  tests/scenarios/scenario_validation_smoke.cpp
)
target_link_libraries(scenario_validation_smoke PRIVATE labops_scenarios)
target_compile_features(scenario_validation_smoke PRIVATE cxx_std_20)
add_test(NAME scenario_validation_smoke COMMAND scenario_validation_smoke)

# Integration smoke test that runs the checked-in baseline scenario through
# `labops run` and asserts output metrics remain within expected ranges.
add_executable(sim_baseline_metrics_integration_smoke
  tests/scenarios/sim_baseline_metrics_integration_smoke.cpp
  src/labops/cli/router.cpp
)
target_include_directories(sim_baseline_metrics_integration_smoke PRIVATE src)
target_link_libraries(sim_baseline_metrics_integration_smoke PRIVATE labops_artifacts labops_backends labops_events labops_metrics labops_scenarios labops_schema)
target_compile_features(sim_baseline_metrics_integration_smoke PRIVATE cxx_std_20)
add_test(NAME sim_baseline_metrics_integration_smoke COMMAND sim_baseline_metrics_integration_smoke)

# Smoke test for FPS computation and metrics.csv emission.
add_executable(fps_metrics_smoke
  tests/metrics/fps_metrics_smoke.cpp
)
target_link_libraries(fps_metrics_smoke PRIVATE labops_artifacts labops_metrics)
target_compile_features(fps_metrics_smoke PRIVATE cxx_std_20)
add_test(NAME fps_metrics_smoke COMMAND fps_metrics_smoke)

# Artifact smoke test for metrics.csv + metrics.json writer contracts.
add_executable(metrics_writers_smoke
  tests/artifacts/metrics_writers_smoke.cpp
)
target_link_libraries(metrics_writers_smoke PRIVATE labops_artifacts labops_metrics)
target_compile_features(metrics_writers_smoke PRIVATE cxx_std_20)
add_test(NAME metrics_writers_smoke COMMAND metrics_writers_smoke)

# Artifact smoke test for static HTML report writer contract.
add_executable(html_report_writer_smoke
  tests/artifacts/html_report_writer_smoke.cpp
)
target_link_libraries(html_report_writer_smoke PRIVATE labops_artifacts labops_metrics labops_schema)
target_compile_features(html_report_writer_smoke PRIVATE cxx_std_20)
add_test(NAME html_report_writer_smoke COMMAND html_report_writer_smoke)

# Smoke test validating that timing/jitter metrics reflect injected jitter.
add_executable(jitter_injection_smoke
  tests/metrics/jitter_injection_smoke.cpp
)
target_link_libraries(jitter_injection_smoke PRIVATE labops_backends labops_metrics)
target_compile_features(jitter_injection_smoke PRIVATE cxx_std_20)
add_test(NAME jitter_injection_smoke COMMAND jitter_injection_smoke)

# Smoke test validating drop metrics match deterministic drop injection knobs.
add_executable(drop_injection_smoke
  tests/metrics/drop_injection_smoke.cpp
)
target_link_libraries(drop_injection_smoke PRIVATE labops_artifacts labops_backends labops_metrics)
target_compile_features(drop_injection_smoke PRIVATE cxx_std_20)
add_test(NAME drop_injection_smoke COMMAND drop_injection_smoke)

# Smoke test for named anomaly heuristics in run-summary highlights.
add_executable(anomaly_detection_smoke
  tests/metrics/anomaly_detection_smoke.cpp
)
target_link_libraries(anomaly_detection_smoke PRIVATE labops_metrics)
target_compile_features(anomaly_detection_smoke PRIVATE cxx_std_20)
add_test(NAME anomaly_detection_smoke COMMAND anomaly_detection_smoke)

# Smoke test validating that host/user identifiers are redacted from host probe
# JSON and raw NIC command outputs when redaction is applied.
add_executable(hostprobe_redaction_smoke
  tests/hostprobe/redaction_smoke.cpp
)
target_link_libraries(hostprobe_redaction_smoke PRIVATE labops_hostprobe)
target_compile_features(hostprobe_redaction_smoke PRIVATE cxx_std_20)
add_test(NAME hostprobe_redaction_smoke COMMAND hostprobe_redaction_smoke)

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
