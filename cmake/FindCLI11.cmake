# Prevent duplicate entries
if(TARGET CLI11::CLI11)
    set(CLI11_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_CLI11_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/CLI11")
get_filename_component(_CLI11_SOURCE_DIR "${_CLI11_SOURCE_DIR}" ABSOLUTE)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_CLI11_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/CLI11-build")

set(CLI11_FOUND TRUE)
