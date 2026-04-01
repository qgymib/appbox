# Prevent duplicate entries
if(TARGET asio)
    set(asio_FOUND TRUE)
    return()
endif()

# Calculate the path to the third-party source code (relative to the directory containing this file)
set(_ASIO_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/asio")
get_filename_component(_ASIO_SOURCE_DIR "${_ASIO_SOURCE_DIR}" ABSOLUTE)

add_library(asio INTERFACE)
target_include_directories(asio
    INTERFACE
        ${_ASIO_SOURCE_DIR}/asio/include
)

set(asio_FOUND TRUE)
