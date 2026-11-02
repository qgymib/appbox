cmake_minimum_required(VERSION 3.15)
project(AppBoxCommon)

# Add cmake search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

###############################################################################
# Export library
#
# Header-only library: every consumer compiles the inline implementations
# directly, there is no library artifact.
#
# The headers include the third-party libraries they use (`tl/expected.hpp`,
# `nlohmann/json.hpp`, `spdlog/spdlog.h` and `zip.h`), but the libraries are not
# part of the interface of this target: a consumer which includes a header links
# the libraries that header needs. This keeps the dependency of every product
# visible at the place which builds it instead of hiding it behind a header
# library.
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
