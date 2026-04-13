cmake_minimum_required(VERSION 3.15)
project(AppBoxLoader)

add_executable(${PROJECT_NAME}
    "rpc/__init__.cpp"
    "rpc/Log.cpp"
    "Loader.cpp"
    "main.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

###############################################################################
# Dependency
###############################################################################
if (NOT TARGET AppBoxCommon)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_BINARY_DIR}/common
    )
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE AppBoxCommon)

find_package(CLI11 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

find_package(Detours REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_compile_options(${PROJECT_NAME} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

###############################################################################
# Build Sandbox as 32-bit and 64-bit dll
###############################################################################
include(ExternalProject)

if(MSVC)
    # Visual Studio 通过 CMAKE_GENERATOR_PLATFORM 切换架构
    set(X86_CMAKE_EXTRA_ARGS
        -A Win32
    )
    set(X64_CMAKE_EXTRA_ARGS)
else()
    # GCC / Clang 通过 -m32 / -m64 切换架构
    # 注意：需要安装 multilib 支持 (gcc-multilib, g++-multilib)
    set(X86_CMAKE_EXTRA_ARGS
        -DCMAKE_C_FLAGS=-m32
        -DCMAKE_CXX_FLAGS=-m32
        -DCMAKE_SHARED_LINKER_FLAGS=-m32
    )
    set(X64_CMAKE_EXTRA_ARGS
        -DCMAKE_C_FLAGS=-m64
        -DCMAKE_CXX_FLAGS=-m64
        -DCMAKE_SHARED_LINKER_FLAGS=-m64
    )
endif()

if(CMAKE_BUILD_TYPE)
    set(FWD_BUILD_TYPE -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
else()
    set(FWD_BUILD_TYPE)
endif()

ExternalProject_Add(sandbox_x86_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x86_build"
    CMAKE_ARGS
        ${X86_CMAKE_EXTRA_ARGS}
        ${FWD_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/sandbox
    BUILD_ALWAYS ON
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/sandbox/lib/AppBoxSandbox32.dll
)

ExternalProject_Add(sandbox_x64_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x64_build"
    CMAKE_ARGS
        ${X64_CMAKE_EXTRA_ARGS}
        ${FWD_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/sandbox
    BUILD_ALWAYS ON
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/sandbox/lib/AppBoxSandbox64.dll
)

###############################################################################
# Embed Sandbox
###############################################################################
add_custom_command(
    OUTPUT  "${CMAKE_BINARY_DIR}/generated/sandbox_x86.c"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT_FILE=${CMAKE_BINARY_DIR}/sandbox/lib/AppBoxSandbox32.dll
        -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/generated/sandbox_x86.c
        -DARRAY_NAME=dll_sandbox_x86
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/bin2h.cmake"
    DEPENDS "${CMAKE_BINARY_DIR}/sandbox/lib/AppBoxSandbox32.dll"
    COMMENT "Embedding sandbox x86 binary into byte array..."
    VERBATIM
)

add_custom_command(
    OUTPUT  "${CMAKE_BINARY_DIR}/generated/sandbox_x64.c"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT_FILE=${CMAKE_BINARY_DIR}/sandbox/lib/AppBoxSandbox64.dll
        -DOUTPUT_FILE=${CMAKE_BINARY_DIR}/generated/sandbox_x64.c
        -DARRAY_NAME=dll_sandbox_x64
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/bin2h.cmake"
    DEPENDS "${CMAKE_BINARY_DIR}/sandbox/lib/AppBoxSandbox64.dll"
    COMMENT "Embedding sandbox x64 binary into byte array..."
    VERBATIM
)

add_custom_target(generate_sandbox_embedded
    DEPENDS
        "${CMAKE_BINARY_DIR}/generated/sandbox_x86.c"
        "${CMAKE_BINARY_DIR}/generated/sandbox_x64.c"
)

target_sources(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated/sandbox_x86.c
        ${CMAKE_BINARY_DIR}/generated/sandbox_x64.c
)
add_dependencies(${PROJECT_NAME} generate_sandbox_embedded)
