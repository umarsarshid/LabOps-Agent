cmake_minimum_required(VERSION 3.20)

# Root build definition for the LabOps CLI. Keep this minimal early so new
# modules can be added incrementally without fighting build-system complexity.
project(labops_agent VERSION 0.1.0 LANGUAGES CXX)

# Standardize on C++20 and disable compiler extensions so behavior stays
# portable across CI platforms (Linux/macOS/Windows).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(labops
  src/labops/cli/router.cpp
  src/labops/main.cpp
)

# Expose `src/` as the include root so includes stay stable as the tree grows.
target_include_directories(labops PRIVATE src)
target_compile_features(labops PRIVATE cxx_std_20)
