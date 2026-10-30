cmake_minimum_required(VERSION 3.15)
project(AppBoxLauncher)

add_executable(${PROJECT_NAME} WIN32
    "control/ControlPlane.cpp"
    "rpc/__init__.cpp"
    "rpc/Environment.cpp"
    "rpc/LauncherStart.cpp"
    "rpc/RunState.cpp"
    "utils/CommandLineOptions.cpp"
    "utils/ConvertDosPathToNtPath.cpp"
    "utils/GetExecutableDir.cpp"
    "utils/HiveMerge.cpp"
    "utils/HostPath.cpp"
    "utils/MapBaseFS.cpp"
    "utils/Md5.cpp"
    "utils/PatchLayer.cpp"
    "utils/ProcessJob.cpp"
    "utils/ProcessJobSet.cpp"
    "utils/SandboxModules.cpp"
    "utils/SelfExtraction.cpp"
    "utils/Shell.cpp"
    "utils/StartupImage.cpp"
    "utils/StartupLayout.cpp"
    "utils/VersionInfo.cpp"
    "utils/ViewPathResolver.cpp"
    "utils/WinCall.cpp"
    "Launcher.cpp"
    "Splash.cpp"
    "main.cpp"
    "resource.rc"
)
appbox_setup_target_wall(${PROJECT_NAME})

# The launcher reads the configuration of a run and the defines it shares with
# the injection modules by their sandbox relative name (`sandbox/Config.hpp`,
# `sandbox/utils/Defines.hpp`), which is why the directory of the sandbox is
# part of the include path. The resolver of the view is a header of `common/`
# and arrives through `AppBoxCommon` below, like the layer mapping, the
# isolation table and the path text helpers it uses.
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox
        ${APPBOX_GENERATED_DIR}
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

# The launcher creates no window of its own. NOMINMAX keeps the min/max macros
# of <windows.h> out of the code, which is what the wx header did before the
# launcher stopped using wxWidgets.
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)

###############################################################################
# Build Sandbox as 32-bit and 64-bit dll
#
# The launcher carries the two sandbox injection modules as RCDATA resources of
# its own executable, so the modules are a build input of this program and not of
# the packer: their builds are configured here, beside the resource script which
# embeds them. A module is built for the bit size which differs from the bit size
# of this build, so each of them is a project of its own below the build tree,
# with the arguments of the build which starts it (see cmake/ForeignBuild.cmake).
#
# Both builds install their module below `resource/lib` of this build tree, which
# is the path the resource script of the launcher names below.
###############################################################################
include(ExternalProject)
include(ForeignBuild)

set(APPBOX_RESOURCE_SANDBOX32 "${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox32.dll")
set(APPBOX_RESOURCE_SANDBOX64 "${CMAKE_BINARY_DIR}/resource/lib/AppBoxSandbox64.dll")

ExternalProject_Add(sandbox_x86_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x86_build"
    CMAKE_ARGS
        ${APPBOX_FOREIGN_X86_ARGS}
        ${APPBOX_FOREIGN_BUILD_TYPE}
        ${APPBOX_FOREIGN_X86_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/resource
    BUILD_BYPRODUCTS ${APPBOX_RESOURCE_SANDBOX32}
    BUILD_ALWAYS ON
)

ExternalProject_Add(sandbox_x64_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_sandbox_x64_build"
    CMAKE_ARGS
        ${APPBOX_FOREIGN_X64_ARGS}
        ${APPBOX_FOREIGN_BUILD_TYPE}
        ${APPBOX_FOREIGN_X64_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/resource
    BUILD_BYPRODUCTS ${APPBOX_RESOURCE_SANDBOX64}
    BUILD_ALWAYS ON
)

###############################################################################
# Payloads embedded as Windows resources
#
# The launcher carries the two sandbox injection modules as RCDATA resources of
# its own executable, so an extracted archive needs no module beside it. The
# modules are the products of the two builds above, so the resource script which
# names them is generated here from cmake/LauncherResources.rc.in, with the
# absolute path of every module substituted for its placeholder.
# launcher/resource.rc includes the generated script.
#
# The resource compiler reads the backslash of a Windows path as an escape
# character inside a string literal, so every separator is doubled before the
# path is substituted.
###############################################################################
appbox_escape_resource_path(APPBOX_RESOURCE_SANDBOX32_RC "${APPBOX_RESOURCE_SANDBOX32}")
appbox_escape_resource_path(APPBOX_RESOURCE_SANDBOX64_RC "${APPBOX_RESOURCE_SANDBOX64}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LauncherResources.rc.in"
    "${APPBOX_GENERATED_DIR}/LauncherResources.generated.rc"
    @ONLY
)

# The modules have to exist before the resource compiler reads them. A Visual
# Studio generator records them as inputs of the resource compiler, so a module
# which changes recompiles the resource script on its own. The other generators
# take the dependency from the source file property instead.
add_dependencies(${PROJECT_NAME} sandbox_x86_build sandbox_x64_build)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/resource.rc"
    PROPERTIES
        OBJECT_DEPENDS "${APPBOX_RESOURCE_SANDBOX32};${APPBOX_RESOURCE_SANDBOX64}"
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

find_package(base64 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE base64)

find_package(CLI11 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

find_package(Detours REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_compile_options(${PROJECT_NAME} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# Known folder resolution of `common/KnownFolder.hpp`, which the launcher copies
# into the injected configuration (SHGetKnownFolderPath and CoTaskMemFree). The
# library used to arrive through the CLI11 target, which is a dependency of the
# command line parser and not of the known folders.
target_link_libraries(${PROJECT_NAME} PRIVATE shell32 ole32)

# Registry access of the hive merge of a patch layer (RegLoadAppKeyW and friends).
target_link_libraries(${PROJECT_NAME} PRIVATE advapi32)

# Version resource of the running program, which carries the file properties of
# the packaged application (GetFileVersionInfoW and VerQueryValueW).
target_link_libraries(${PROJECT_NAME} PRIVATE version)

# Imaging component of Windows, which decodes the startup image of a session
# (IWICImagingFactory and friends); it is a component of the operating system
# and not a library of the project. The blend function which draws the image
# with its transparency lives in msimg32.
target_link_libraries(${PROJECT_NAME} PRIVATE windowscodecs ole32 msimg32)

# Digest of a patch package (BCryptOpenAlgorithmProvider and friends).
target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt)

###############################################################################
# Export for the packer
#
# Copy the built launcher below the build resource tree, where the resource script
# of the packer names it (see the `Payloads embedded as Windows resources`
# section of the top level CMakeLists.txt).
###############################################################################
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/resource/appbox"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_BINARY_DIR}/resource/appbox/AppBoxLauncher.exe"
    COMMENT "Copying AppBoxLauncher.exe for the packer resource"
)
