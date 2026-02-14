# FindVendorSDK.cmake
#
# Discovers local vendor SDK layout without hard-coding proprietary header names
# or library filenames in this open repository.
#
# Supported inputs (CMake cache vars or environment variables):
# - VENDOR_SDK_ROOT
# - VENDOR_SDK_INCLUDE
# - VENDOR_SDK_LIB
#
# Outputs:
# - VendorSDK_FOUND
# - VendorSDK_ROOT_DIR
# - VendorSDK_INCLUDE_DIR
# - VendorSDK_LIBRARY_DIR

function(_labops_pick_first_existing_dir out_var)
  foreach(candidate IN LISTS ARGN)
    if(NOT candidate STREQUAL "" AND IS_DIRECTORY "${candidate}")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

set(_vendor_sdk_root_candidates)
if(DEFINED VENDOR_SDK_ROOT AND NOT VENDOR_SDK_ROOT STREQUAL "")
  list(APPEND _vendor_sdk_root_candidates "${VENDOR_SDK_ROOT}")
endif()
if(DEFINED ENV{VENDOR_SDK_ROOT} AND NOT "$ENV{VENDOR_SDK_ROOT}" STREQUAL "")
  list(APPEND _vendor_sdk_root_candidates "$ENV{VENDOR_SDK_ROOT}")
endif()

set(_vendor_sdk_include_candidates)
if(DEFINED VENDOR_SDK_INCLUDE AND NOT VENDOR_SDK_INCLUDE STREQUAL "")
  list(APPEND _vendor_sdk_include_candidates "${VENDOR_SDK_INCLUDE}")
endif()
if(DEFINED ENV{VENDOR_SDK_INCLUDE} AND NOT "$ENV{VENDOR_SDK_INCLUDE}" STREQUAL "")
  list(APPEND _vendor_sdk_include_candidates "$ENV{VENDOR_SDK_INCLUDE}")
endif()
foreach(root_dir IN LISTS _vendor_sdk_root_candidates)
  list(APPEND _vendor_sdk_include_candidates "${root_dir}/include")
endforeach()

set(_vendor_sdk_lib_candidates)
if(DEFINED VENDOR_SDK_LIB AND NOT VENDOR_SDK_LIB STREQUAL "")
  list(APPEND _vendor_sdk_lib_candidates "${VENDOR_SDK_LIB}")
endif()
if(DEFINED ENV{VENDOR_SDK_LIB} AND NOT "$ENV{VENDOR_SDK_LIB}" STREQUAL "")
  list(APPEND _vendor_sdk_lib_candidates "$ENV{VENDOR_SDK_LIB}")
endif()
foreach(root_dir IN LISTS _vendor_sdk_root_candidates)
  list(APPEND _vendor_sdk_lib_candidates "${root_dir}/lib" "${root_dir}/lib64")
endforeach()

_labops_pick_first_existing_dir(VendorSDK_ROOT_DIR ${_vendor_sdk_root_candidates})
_labops_pick_first_existing_dir(VendorSDK_INCLUDE_DIR ${_vendor_sdk_include_candidates})
_labops_pick_first_existing_dir(VendorSDK_LIBRARY_DIR ${_vendor_sdk_lib_candidates})

if(VendorSDK_ROOT_DIR STREQUAL "")
  if(NOT VendorSDK_INCLUDE_DIR STREQUAL "" AND NOT VendorSDK_LIBRARY_DIR STREQUAL "")
    get_filename_component(_vendor_sdk_include_parent "${VendorSDK_INCLUDE_DIR}" DIRECTORY)
    get_filename_component(_vendor_sdk_library_parent "${VendorSDK_LIBRARY_DIR}" DIRECTORY)
    if(_vendor_sdk_include_parent STREQUAL _vendor_sdk_library_parent)
      set(VendorSDK_ROOT_DIR "${_vendor_sdk_include_parent}")
    endif()
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  VendorSDK
  REQUIRED_VARS VendorSDK_INCLUDE_DIR VendorSDK_LIBRARY_DIR
)

mark_as_advanced(VendorSDK_ROOT_DIR VendorSDK_INCLUDE_DIR VendorSDK_LIBRARY_DIR)
