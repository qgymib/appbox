###############################################################################
# Export:
# - ZLIB::ZLIBSTATIC
# - ZLIB::ZLIB
###############################################################################

# Prevent duplicate entries
if(TARGET ZLIB::ZLIBSTATIC)
    set(ZLIB_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_ZLIB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/zlib")
get_filename_component(_ZLIB_SOURCE_DIR "${_ZLIB_SOURCE_DIR}" ABSOLUTE)

set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_BUILD_STATIC ON)
set(ZLIB_INSTALL OFF)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_ZLIB_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/zlib-build")

add_library(ZLIB::ZLIB ALIAS zlibstatic)

set(ZLIB_FOUND TRUE)
