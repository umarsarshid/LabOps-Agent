cmake_minimum_required(VERSION 3.20)

# Root build definition for the LabOps CLI. Keep this minimal early so new
# modules can be added incrementally without fighting build-system complexity.
project(labops_agent VERSION 0.1.0 LANGUAGES CXX)

# Standardize on C++20 and disable compiler extensions so behavior stays
# portable across CI platforms (Linux/macOS/Windows).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared schema library for run contracts. Keeping this in a standalone target
# makes it easy for CLI, tests, and future modules to reuse the same contract
# and serialization behavior.
add_library(labops_schema
  src/core/schema/run_contract.cpp
)
target_include_directories(labops_schema PUBLIC src)
target_compile_features(labops_schema PUBLIC cxx_std_20)

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
target_link_libraries(labops PRIVATE labops_artifacts labops_schema)

# Early smoke-test target that validates the schema contract and JSON output
# remain stable. This is intentionally lightweight until broader test
# infrastructure is introduced.
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
