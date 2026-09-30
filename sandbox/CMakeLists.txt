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
    "environment/Configuration.cpp"
    "environment/Isolation.cpp"
    "environment/Table.cpp"
    "filesystem/__init__.cpp"
    "filesystem/CreateDirectory.cpp"
    "filesystem/DirectoryMerge.cpp"
    "filesystem/DirName.cpp"
    "filesystem/Isolation.cpp"
    "filesystem/IsolationTable.cpp"
    "filesystem/RemoveAll.cpp"
    "filesystem/Resolve.cpp"
    "filesystem/Sequence.cpp"
    "fonts/Isolation.cpp"
    "hook/__init__.cpp"
    "hook/closesocket.cpp"
    "hook/connect.cpp"
    "hook/CreateProcessInternalW.cpp"
    "hook/DnsQuery_A.cpp"
    "hook/DnsQuery_UTF8.cpp"
    "hook/DnsQuery_W.cpp"
    "hook/ExpandEnvironmentStringsA.cpp"
    "hook/ExpandEnvironmentStringsW.cpp"
    "hook/FreeEnvironmentStringsA.cpp"
    "hook/FreeEnvironmentStringsW.cpp"
    "hook/getaddrinfo.cpp"
    "hook/GetAddrInfoExW.cpp"
    "hook/GetAddrInfoW.cpp"
    "hook/GetEnvironmentStringsA.cpp"
    "hook/GetEnvironmentStringsW.cpp"
    "hook/GetEnvironmentVariableA.cpp"
    "hook/GetEnvironmentVariableW.cpp"
    "hook/gethostbyname.cpp"
    "hook/LdrQueryImageFileExecutionOptionsEx.cpp"
    "hook/NtClose.cpp"
    "hook/NtCreateFile.cpp"
    "hook/NtCreateKey.cpp"
    "hook/NtCurrentTeb.cpp"
    "hook/NtDeleteFile.cpp"
    "hook/NtDeleteKey.cpp"
    "hook/NtDeleteValueKey.cpp"
    "hook/NtDeviceIoControlFile.cpp"
    "hook/NtEnumerateKey.cpp"
    "hook/NtEnumerateValueKey.cpp"
    "hook/NtFsControlFile.cpp"
    "hook/NtGdiAddFontResourceW.cpp"
    "hook/NtGdiRemoveFontResourceW.cpp"
    "hook/NtOpenFile.cpp"
    "hook/NtOpenKey.cpp"
    "hook/NtOpenKeyEx.cpp"
    "hook/NtQueryAttributesFile.cpp"
    "hook/NtQueryDirectoryFile.cpp"
    "hook/NtQueryDirectoryFileEx.cpp"
    "hook/NtQueryFullAttributesFile.cpp"
    "hook/NtQueryInformationByName.cpp"
    "hook/NtQueryInformationFile.cpp"
    "hook/NtQueryKey.cpp"
    "hook/NtQueryMultipleValueKey.cpp"
    "hook/NtQueryObject.cpp"
    "hook/NtQueryValueKey.cpp"
    "hook/NtQueryVolumeInformationFile.cpp"
    "hook/NtReadFile.cpp"
    "hook/NtSaveKey.cpp"
    "hook/NtSaveKeyEx.cpp"
    "hook/NtSetInformationFile.cpp"
    "hook/NtWriteFile.cpp"
    "hook/recvfrom.cpp"
    "hook/RtlCompareUnicodeString.cpp"
    "hook/RtlCreateEnvironment.cpp"
    "hook/RtlExpandEnvironmentStrings_U.cpp"
    "hook/RtlInitUnicodeString.cpp"
    "hook/RtlQueryEnvironmentVariable.cpp"
    "hook/RtlQueryEnvironmentVariable_U.cpp"
    "hook/RtlSetEnvironmentVariable.cpp"
    "hook/sendto.cpp"
    "hook/SetEnvironmentVariableA.cpp"
    "hook/SetEnvironmentVariableW.cpp"
    "hook/SetProcessMitigationPolicy.cpp"
    "hook/WSAConnect.cpp"
    "hook/WSARecvFrom.cpp"
    "hook/WSASendTo.cpp"
    "network/DnsTable.cpp"
    "network/Isolation.cpp"
    "network/Proxy.cpp"
    "network/ProxyConfig.cpp"
    "network/Socks5.cpp"
    "registry/__init__.cpp"
    "registry/EnumMerge.cpp"
    "registry/IsolationTable.cpp"
    "registry/KeyPath.cpp"
    "registry/RootMap.cpp"
    "registry/Whiteout.cpp"
    "utils/BitParser.cpp"
    "utils/CheckPathExist.cpp"
    "utils/ConvertToFullNtPath.cpp"
    "utils/CopyFileNt.cpp"
    "utils/CrashReport.cpp"
    "utils/DirectoryInformationWalker.cpp"
    "utils/FontApi.cpp"
    "utils/GetPEB.cpp"
    "utils/HandleInfo.cpp"
    "utils/HookTransaction.cpp"
    "utils/Log.cpp"
    "utils/LogFile.cpp"
    "utils/MappingAsDosNtPath.cpp"
    "utils/MappingAsSandboxNtPath.cpp"
    "utils/NameResolution.cpp"
    "utils/PipeClient.cpp"
    "utils/ProxyHook.cpp"
    "utils/QueryHandlePath.cpp"
    "utils/Task.cpp"
    "utils/VariableExpansion.cpp"
    "ModuleTable.cpp"
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
