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

# Artifact writers convert in-memory run contracts into stable on-disk outputs.
add_library(labops_artifacts
  src/artifacts/run_writer.cpp
)
target_include_directories(labops_artifacts PUBLIC src)
target_compile_features(labops_artifacts PUBLIC cxx_std_20)
target_link_libraries(labops_artifacts PUBLIC labops_schema)

add_executable(labops
  src/labops/cli/router.cpp
  src/labops/main.cpp
)

# Expose `src/` as the include root so includes stay stable as the tree grows.
target_include_directories(labops PRIVATE src)
target_compile_features(labops PRIVATE cxx_std_20)
target_link_libraries(labops PRIVATE labops_artifacts labops_backends labops_events labops_schema)

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
target_link_libraries(run_stream_trace_smoke PRIVATE labops_artifacts labops_backends labops_events labops_schema)
target_compile_features(run_stream_trace_smoke PRIVATE cxx_std_20)
add_test(NAME run_stream_trace_smoke COMMAND run_stream_trace_smoke)

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
