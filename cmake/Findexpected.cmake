###############################################################################
# Export:
# - tl::expected
###############################################################################
if(TARGET tl::expected)
    set(expected_FOUND TRUE)
    return()
endif()

set(_EXPECTED_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/expected")
get_filename_component(_EXPECTED_SOURCE_DIR "${_EXPECTED_SOURCE_DIR}" ABSOLUTE)

set(EXPECTED_BUILD_PACKAGE OFF)
set(EXPECTED_BUILD_TESTS OFF)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_EXPECTED_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/expected-build")

set(expected_FOUND TRUE)
