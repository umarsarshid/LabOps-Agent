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
  src/backends/sim/scenario_config.cpp
  src/backends/sim/sim_camera_backend.cpp
)
target_include_directories(labops_backends PUBLIC src)
target_compile_features(labops_backends PUBLIC cxx_std_20)

# Scenario loader/validation module.
add_library(labops_scenarios
  src/scenarios/validator.cpp
)
target_include_directories(labops_scenarios PUBLIC src)
target_compile_features(labops_scenarios PUBLIC cxx_std_20)

# Metrics formulas and writers for run performance outputs.
add_library(labops_metrics
  src/metrics/fps.cpp
)
target_include_directories(labops_metrics PUBLIC src)
target_compile_features(labops_metrics PUBLIC cxx_std_20)

# Artifact writers convert in-memory run contracts into stable on-disk outputs.
add_library(labops_artifacts
  src/artifacts/bundle_manifest_writer.cpp
  src/artifacts/bundle_zip_writer.cpp
  src/artifacts/metrics_diff_writer.cpp
  src/artifacts/metrics_writer.cpp
  src/artifacts/run_summary_writer.cpp
  src/artifacts/scenario_writer.cpp
  src/artifacts/run_writer.cpp
)
target_include_directories(labops_artifacts PUBLIC src)
target_compile_features(labops_artifacts PUBLIC cxx_std_20)
target_link_libraries(labops_artifacts PUBLIC labops_metrics labops_schema)

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
