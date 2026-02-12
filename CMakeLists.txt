cmake_minimum_required(VERSION 3.20)

project(labops_agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(labops
  src/labops/cli/router.cpp
  src/labops/main.cpp
)

target_include_directories(labops PRIVATE src)
target_compile_features(labops PRIVATE cxx_std_20)
