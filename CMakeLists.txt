cmake_minimum_required(VERSION 3.15)
project(appbox)

# Add cmake search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmrc")

###############################################################################
# Support functions
###############################################################################
function(appbox_setup_target_wall name)
    if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${name} PRIVATE /W4 /WX)
    else ()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    endif ()
endfunction()

###############################################################################
# Build Sandbox as 32-bit and 64-bit dll
###############################################################################
include(ExternalProject)
include(CMakeRC)

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
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x86_build"
    CMAKE_ARGS
        ${X86_CMAKE_EXTRA_ARGS}
        ${FWD_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/resource
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox32.dll
)

ExternalProject_Add(sandbox_x64_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x64_build"
    CMAKE_ARGS
        ${X64_CMAKE_EXTRA_ARGS}
        ${FWD_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/resource
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox64.dll
)

cmrc_add_resource_library(sandbox_resource
    WHENCE ${CMAKE_BINARY_DIR}/resource
    ${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox32.dll
    ${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox64.dll
)

###############################################################################
# Project products
###############################################################################

add_subdirectory(loader)

include (CTest)
add_subdirectory(test)
