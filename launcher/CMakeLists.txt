cmake_minimum_required(VERSION 3.15)
project(AppBoxLauncher)

add_executable(${PROJECT_NAME} WIN32
    "rpc/__init__.cpp"
    "rpc/Environment.cpp"
    "utils/CommandLineOptions.cpp"
    "utils/ConvertDosPathToNtPath.cpp"
    "utils/GetExecutableDir.cpp"
    "utils/HiveMerge.cpp"
    "utils/KnownFolder.cpp"
    "utils/MapBaseFS.cpp"
    "utils/Md5.cpp"
    "utils/PatchLayer.cpp"
    "utils/ProcessJob.cpp"
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

    # The resolver of the view. A startup file names a path of the sandbox
    # view, which usually exists in the layers of the archive only, so the
    # launcher locates the image with the very same rules a sandboxed process
    # sees: the shared implementation and the pure helpers it uses are
    # compiled into the launcher as well.
    "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/filesystem/DirName.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/filesystem/IsolationTable.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/filesystem/ResolveCore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/filesystem/Sequence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/MappingAsDosNtPath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/MappingAsSandboxNtPath.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

# The sandbox directory is added last, so the headers of the launcher win: the
# shared resolver includes the filesystem headers of the sandbox by their
# sandbox relative name (`filesystem/ResolveCore.hpp`, `utils/MappingAs*.hpp`).
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
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
# Payloads embedded as Windows resources
#
# The launcher carries the two sandbox injection modules as RCDATA resources of
# its own executable, so an extracted archive needs no module beside it. The
# modules are build products, so the resource script which names them is
# generated here from cmake/LauncherResources.rc.in, with the absolute path of
# every module substituted for its placeholder. launcher/resource.rc includes
# the generated script.
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
