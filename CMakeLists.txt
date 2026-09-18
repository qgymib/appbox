cmake_minimum_required(VERSION 3.15)
project(appbox VERSION 0.1.0)

###############################################################################
# Dependency library form
#
# Every third-party dependency is built as a static library and linked into the
# executables; the sandbox injection modules (AppBoxSandbox32/64.dll) are the
# only dynamic artifacts. The FORCE keyword also heals build trees whose cache
# already holds a stale ON value, for example one created by a third-party
# option() before this guard existed.
###############################################################################
set(BUILD_SHARED_LIBS OFF CACHE BOOL "appbox: build all dependencies as static libraries" FORCE)

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
    set(X86_CMAKE_EXTRA_ARGS)
    set(X64_CMAKE_EXTRA_ARGS)
endif()

if(CMAKE_BUILD_TYPE)
    set(FWD_BUILD_TYPE -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
else()
    set(FWD_BUILD_TYPE)
endif()

if (CMAKE_TOOLCHAIN_FILE)
    set(X86_FWD_TOOLCHAIN_FILE -DCMAKE_TOOLCHAIN_FILE=${APPBOX_X86_TOOLCHAIN_FILE})
    set(X64_FWD_TOOLCHAIN_FILE -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
else()
    set(X86_FWD_TOOLCHAIN_FILE)
    set(X64_FWD_TOOLCHAIN_FILE)
endif()

ExternalProject_Add(sandbox_x86_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x86_build"
    CMAKE_ARGS
        ${X86_CMAKE_EXTRA_ARGS}
        ${FWD_BUILD_TYPE}
        ${X86_FWD_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/resource
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox32.dll
    BUILD_ALWAYS ON
)

ExternalProject_Add(sandbox_x64_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x64_build"
    CMAKE_ARGS
        ${X64_CMAKE_EXTRA_ARGS}
        ${FWD_BUILD_TYPE}
        ${X64_FWD_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/resource
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox64.dll
    BUILD_ALWAYS ON
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

###############################################################################
# AppBox
#
# The main product is described here instead of in a src/CMakeLists.txt so that
# the build keeps a single entry point; src/ is a pure source directory. The
# target name is spelled out because ${PROJECT_NAME} holds the name of the top
# level project (appbox).
###############################################################################
set(APPBOX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(AppBox WIN32
    "${APPBOX_SOURCE_DIR}/core/ApplicationIcon.cpp"
    "${APPBOX_SOURCE_DIR}/core/BuildReport.cpp"
    "${APPBOX_SOURCE_DIR}/core/PackModel.cpp"
    "${APPBOX_SOURCE_DIR}/core/PackService.cpp"
    "${APPBOX_SOURCE_DIR}/core/PresetDirectory.cpp"
    "${APPBOX_SOURCE_DIR}/core/StartupTree.cpp"
    "${APPBOX_SOURCE_DIR}/core/ZipReader.cpp"
    "${APPBOX_SOURCE_DIR}/core/ZipWriter.cpp"
    "${APPBOX_SOURCE_DIR}/widget/FilesystemPanel.cpp"
    "${APPBOX_SOURCE_DIR}/widget/LoaderResource.cpp"
    "${APPBOX_SOURCE_DIR}/widget/MainFrame.cpp"
    "${APPBOX_SOURCE_DIR}/widget/MainProgramDialog.cpp"
    "${APPBOX_SOURCE_DIR}/widget/PlaceholderPanel.cpp"
    "${APPBOX_SOURCE_DIR}/widget/RibbonBar.cpp"
    "${APPBOX_SOURCE_DIR}/widget/SideNav.cpp"
    "${APPBOX_SOURCE_DIR}/widget/StartupCheckRenderer.cpp"
    "${APPBOX_SOURCE_DIR}/widget/StartupTreeModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/loader/utils/KnownFolder.cpp"
    "${APPBOX_SOURCE_DIR}/main.cpp"
    "${APPBOX_SOURCE_DIR}/resource.rc"
)
appbox_setup_target_wall(AppBox)

target_include_directories(AppBox
    PRIVATE
        "${APPBOX_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/loader"
)

target_compile_features(AppBox
    PRIVATE
        cxx_std_17
)

###############################################################################
# Application manifest
#
# resource.rc embeds the wxWidgets application manifest (per monitor v2 DPI
# awareness) through wx/msw/wx.rc. The manifest generated by the linker would be
# a second RT_MANIFEST resource with the same id, which makes cvtres fail with
# CVT1100, so it is turned off here. The wx manifest already carries the
# trustInfo element (level="asInvoker") of the default one.
###############################################################################
if (MSVC)
    target_link_options(AppBox PRIVATE /MANIFEST:NO)
endif ()

###############################################################################
# Dependency
###############################################################################
if (NOT TARGET AppBoxCommon)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_BINARY_DIR}/common
    )
endif ()
target_link_libraries(AppBox PRIVATE AppBoxCommon)

find_package(spdlog REQUIRED)
target_link_libraries(AppBox PRIVATE spdlog::spdlog)
target_compile_options(AppBox PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

find_package(wxWidgets REQUIRED)
target_link_libraries(AppBox PRIVATE wx::core wx::base wx::adv wx::ribbon)

# Known folder resolution of the loader helper (SHGetKnownFolderPath).
target_link_libraries(AppBox PRIVATE shell32 ole32)

# zlib must be configured before libzip: the libzip build performs its own
# find_package(ZLIB 1.1.2 REQUIRED) which reuses the already built target.
find_package(ZLIB REQUIRED)
find_package(libzip REQUIRED)
target_link_libraries(AppBox PRIVATE libzip::zip)

###############################################################################
# Embed the loader
#
# The loader copies itself below the build resource tree (POST_BUILD command of
# AppBoxLoader), and CMakeRC embeds the copied bytes into this executable. The
# resource intermediate file is generated at build time, so the loader binary
# does not need to exist at configure time; the target dependency keeps the
# embedding order correct.
###############################################################################
cmrc_add_resource_library(appbox_resource
    WHENCE ${CMAKE_BINARY_DIR}/resource/appbox
    ${CMAKE_BINARY_DIR}/resource/appbox/AppBoxLoader.exe
)
add_dependencies(appbox_resource AppBoxLoader)
target_link_libraries(AppBox
    PRIVATE
        appbox_resource
)

include (CTest)
add_subdirectory(test)

###############################################################################
# Install products
###############################################################################
include(CPack)
install(TARGETS AppBox
    DESTINATION bin
)
