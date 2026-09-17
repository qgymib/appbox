cmake_minimum_required(VERSION 3.15)
project(AppBoxCommon)

# Add cmake search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

###############################################################################
# Export library
#
# Header-only library: every consumer compiles the inline implementations
# directly, there is no library artifact.
###############################################################################
add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(${PROJECT_NAME}
    INTERFACE
        cxx_std_17
)

###############################################################################
# Dependency
###############################################################################
find_package(asio REQUIRED)
target_link_libraries(${PROJECT_NAME} INTERFACE asio ws2_32)

find_package(expected REQUIRED)
target_link_libraries(${PROJECT_NAME} INTERFACE tl::expected)

find_package(nlohmann_json REQUIRED)
target_link_libraries(${PROJECT_NAME} INTERFACE nlohmann_json::nlohmann_json)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} INTERFACE spdlog::spdlog)
