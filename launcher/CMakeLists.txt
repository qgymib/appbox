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
    "utils/ProcessLauncher.cpp"
    "utils/Shell.cpp"
    "utils/WinCall.cpp"
    "Launcher.cpp"
    "main.cpp"
    "resource.rc"

    # The reader half of the archive layout of the packer. The launcher reads
    # the patch packages of the user with it, so a package is extracted with
    # the very same rules the packer writes it with, including the rejection
    # of entry names which would escape the destination folder.
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ZipReader.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
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

# Digest of a patch package (BCryptOpenAlgorithmProvider and friends).
target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt)

# Extraction of a patch package. zlib must be configured before libzip: the
# libzip build performs its own find_package(ZLIB 1.1.2 REQUIRED) which reuses
# the already configured target.
find_package(ZLIB REQUIRED)
find_package(libzip REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE libzip::zip)

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
