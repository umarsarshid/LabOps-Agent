# cmake

Custom CMake modules used by LabOps.

## Why this folder exists

Some build logic is easier to keep in dedicated modules instead of growing the
root `CMakeLists.txt`. This keeps discovery logic readable and testable.

## Current contents

- `FindVendorSDK.cmake`
  - discovers local vendor SDK paths for real-backend integration plumbing
  - supports both CMake cache vars and environment variables:
    - `VENDOR_SDK_ROOT`
    - `VENDOR_SDK_INCLUDE`
    - `VENDOR_SDK_LIB`

## Connection to the project

This module lets LabOps cleanly detect whether a local real SDK environment is
available, while keeping CI and default builds portable when SDK files are not
installed.
