# Prevent duplicate entries
if(TARGET nlohmann_json::nlohmann_json)
    set(nlohmann_json_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_NLOHMANN_JSON_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/nlohmann_json")
get_filename_component(_NLOHMANN_JSON_SOURCE_DIR "${_NLOHMANN_JSON_SOURCE_DIR}" ABSOLUTE)

set(JSON_BuildTests OFF)
set(JSON_Install OFF)

# Build the third-party library (specify the binary directory to avoid conflicts)
add_subdirectory("${_NLOHMANN_JSON_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-build")

set(nlohmann_json_FOUND TRUE)
