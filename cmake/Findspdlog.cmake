# Prevent duplicate entries
if(TARGET spdlog::spdlog)
    set(spdlog_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_SPDLOG_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/spdlog")
get_filename_component(_SPDLOG_SOURCE_DIR "${_SPDLOG_SOURCE_DIR}" ABSOLUTE)

# Build options
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_WCHAR_SUPPORT ON)
set(SPDLOG_WCHAR_FILENAMES ON)
set(SPDLOG_WCHAR_CONSOLE ON)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_SPDLOG_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/spdlog-build")

set(spdlog_FOUND TRUE)
