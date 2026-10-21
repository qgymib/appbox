cmake_minimum_required(VERSION 3.15)
project(AppBoxProbe)

# Add cmake search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")

include(SetupTargetWall)

# The probe runs inside the sandbox of a case, so it links the runtime library of
# the compiler statically like every other product of the repository; see
# cmake/StaticRuntime.cmake. The file has to be included before the first target
# is created.
include(StaticRuntime)

###############################################################################
# Bit size of the probe
#
# The project is configured once per bit size: the 64 bit executable is the probe
# of the cases by default, the 32 bit one lets a case run its operations inside a
# 32 bit process. The name of the target carries the bit size, like the names of
# the sandbox injection modules, so both executables can live in one directory.
###############################################################################
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(APPBOX_PROBE_SUFFIX 64)
else ()
    set(APPBOX_PROBE_SUFFIX 32)
endif ()

set(probe_name ${PROJECT_NAME}${APPBOX_PROBE_SUFFIX})

###############################################################################
# Build probe
#
# The probe is the program the end-to-end cases run inside the sandbox: it asks
# the test process for its task over the named pipe of the case and reports the
# result back. The source files are an explicit list, not a glob, so a new probe
# has to be registered here.
###############################################################################
add_executable(${probe_name}
    "__init__.cpp"
    "ConsoleWindow.cpp"
    "CreateDirectoryW.cpp"
    "CreateFileW.cpp"
    "DeleteFileW.cpp"
    "EnvironmentRead.cpp"
    "EnvironmentWrite.cpp"
    "FindStreams.cpp"
    "Fonts.cpp"
    "LaunchProcess.cpp"
    "ListDir.cpp"
    "ListDirNt.cpp"
    "LoadedModules.cpp"
    "ParamCheck.cpp"
    "QueryAttributes.cpp"
    "QueryFullAttributes.cpp"
    "QueryInformationByName.cpp"
    "QueryInformationFile.cpp"
    "RemoveDirectory.cpp"
    "ReparsePoint.cpp"
    "ReadFileFull.cpp"
    "RegChildProcess.cpp"
    "RegDeleteKey.cpp"
    "RegDeleteValue.cpp"
    "RegEnumKey.cpp"
    "RegEnumValue.cpp"
    "RegKeyApi.cpp"
    "RegOpenWriteValue.cpp"
    "RegQueryKeyName.cpp"
    "RegQueryMultipleValues.cpp"
    "RegReadValue.cpp"
    "RegReadValues.cpp"
    "RegRenameKey.cpp"
    "RegSaveKey.cpp"
    "RegShadowRead.cpp"
    "RegTransacted.cpp"
    "RegTwoProcesses.cpp"
    "RegWriteValue.cpp"
    "WriteFile.cpp"
    "ResolveName.cpp"
    "SetInformationFile.cpp"
    "SocketTraffic.cpp"
    "StartupStarted.cpp"
    "main.cpp"

    # Helpers the probe shares with the test executable. They only call the API
    # of the system, so they run in the process of the probe as they are.
    "${CMAKE_CURRENT_SOURCE_DIR}/../utils/NtDirectoryQuery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../utils/ReadFileFull.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../utils/RegistryRootKey.cpp"

    # The probe names the environment of the process the way the runtime of the
    # operating system does, so it reads the address of the environment pointer
    # of the process parameters with the helper of the sandbox.
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sandbox/utils/GetPEB.cpp"
)
setup_target_wall(${probe_name})
set_target_properties(${probe_name}
    PROPERTIES
        CXX_STANDARD 17
)

target_include_directories(${probe_name}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../launcher
        ${CMAKE_CURRENT_SOURCE_DIR}/../../sandbox
)

###############################################################################
# Dependency
###############################################################################
if (NOT TARGET AppBoxCommon)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_BINARY_DIR}/common
    )
endif ()
target_link_libraries(${probe_name} PRIVATE AppBoxCommon)

find_package(CLI11 REQUIRED)
target_link_libraries(${probe_name} PRIVATE CLI11::CLI11)

find_package(spdlog REQUIRED)
target_link_libraries(${probe_name} PRIVATE spdlog::spdlog)

# The name resolution probe calls the DNS client of the system (DnsQuery_*), the
# font probe the font API (AddFontResourceExW, EnumFontFamiliesExW), the console
# probe the window API (IsWindowVisible) and the registry probes the registry
# API, which also carries the SID conversion (sddl.h).
target_link_libraries(${probe_name} PRIVATE dnsapi gdi32 user32 advapi32)

# The command line of the MinGW entry point is split with CommandLineToArgvW.
target_link_libraries(${probe_name} PRIVATE shell32)

###############################################################################
# Install
#
# The executable is installed below the install prefix the test project passes
# to this build, beside the directory the injection modules are installed to:
# the cases are started with the path of the installed executable, so the
# prefix is the one the test project names and this project only names the
# directory below it. The path of the executable is therefore independent of the
# generator and of the configuration, which is what a single config generator
# (MinGW Makefiles, Ninja) and a multi config one (Visual Studio) both need.
#
# The probe is not part of the package of the repository: the only product which
# is installed is the packer, and the install step of this project fills the
# build tree only.
###############################################################################
install(TARGETS ${probe_name}
    RUNTIME DESTINATION probe
)
