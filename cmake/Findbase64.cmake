###############################################################################
# Export:
# - base64
###############################################################################

# Prevent duplicate entries
if(TARGET base64)
    set(base64_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_BASE64_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/base64")
get_filename_component(_BASE64_SOURCE_DIR "${_BASE64_SOURCE_DIR}" ABSOLUTE)

set(BASE64_ENABLE_TESTING OFF)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_BASE64_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/base64-build")

set(base64_FOUND TRUE)
