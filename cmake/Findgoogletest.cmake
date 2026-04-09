# Prevent duplicate entries
if(TARGET gtest)
    set(googletest_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_GOOGLETEST_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/googletest")
get_filename_component(_GOOGLETEST_SOURCE_DIR "${_GOOGLETEST_SOURCE_DIR}" ABSOLUTE)

set(INSTALL_GTEST OFF)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_GOOGLETEST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/googletest-build")

# Export as CMake target
add_library(googletest::gtest ALIAS gtest)

set(googletest_FOUND TRUE)
