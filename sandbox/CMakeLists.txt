cmake_minimum_required(VERSION 3.15)
project(AppBoxSandbox)

# Add cmake search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

include(SetupTargetWall)

###############################################################################
# Build sandbox module
###############################################################################
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(APPBOX_SANDBOX_SUFFIX 64)
else ()
    set(APPBOX_SANDBOX_SUFFIX 32)
endif ()

set(sandbox_name ${PROJECT_NAME}${APPBOX_SANDBOX_SUFFIX})

add_library(${sandbox_name} MODULE
    "filesystem/__init__.cpp"
    "filesystem/CreateDirectory.cpp"
    "filesystem/DirName.cpp"
    "filesystem/RemoveAll.cpp"
    "filesystem/Resolve.cpp"
    "filesystem/Sequence.cpp"
    "hook/__init__.cpp"
    "hook/CreateProcessInternalW.cpp"
    "hook/LdrQueryImageFileExecutionOptionsEx.cpp"
    "hook/NtClose.cpp"
    "hook/NtCreateFile.cpp"
    "hook/NtCurrentTeb.cpp"
    "hook/NtDeleteFile.cpp"
    "hook/NtDeviceIoControlFile.cpp"
    "hook/NtFsControlFile.cpp"
    "hook/NtOpenFile.cpp"
    "hook/NtQueryAttributesFile.cpp"
    "hook/NtQueryDirectoryFile.cpp"
    "hook/NtQueryDirectoryFileEx.cpp"
    "hook/NtQueryFullAttributesFile.cpp"
    "hook/NtQueryInformationByName.cpp"
    "hook/NtQueryInformationFile.cpp"
    "hook/NtQueryObject.cpp"
    "hook/NtQueryVolumeInformationFile.cpp"
    "hook/NtReadFile.cpp"
    "hook/NtSetInformationFile.cpp"
    "hook/NtWriteFile.cpp"
    "hook/RtlInitUnicodeString.cpp"
    "hook/SetProcessMitigationPolicy.cpp"
    "utils/BitParser.cpp"
    "utils/CheckPathExist.cpp"
    "utils/CopyFileNt.cpp"
    "utils/GetPEB.cpp"
    "utils/HandleInfo.cpp"
    "utils/Log.cpp"
    "utils/MappingAsDosNtPath.cpp"
    "utils/MappingAsSandboxNtPath.cpp"
    "utils/PipeClient.cpp"
    "utils/Task.cpp"
    "Sandbox.cpp"
    "Sandbox.def"
)

setup_target_wall(${sandbox_name})
set_target_properties(${sandbox_name}
    PROPERTIES
        CXX_STANDARD 17
        PREFIX ""
)

target_include_directories(${sandbox_name}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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
target_link_libraries(${sandbox_name} PRIVATE AppBoxCommon)

find_package(Detours REQUIRED)
target_link_libraries(${sandbox_name} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${sandbox_name} PRIVATE spdlog::spdlog)
target_compile_definitions(${sandbox_name} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

###############################################################################
# Install
###############################################################################

install(TARGETS ${sandbox_name}
    LIBRARY DESTINATION lib
)
