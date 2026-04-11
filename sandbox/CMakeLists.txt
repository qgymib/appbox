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
    "hook/__init__.cpp"
    "hook/CreateProcessInternalW.cpp"
    "hook/LdrQueryImageFileExecutionOptionsEx.cpp"
    "hook/NtCreateFile.cpp"
    "hook/NtQueryObject.cpp"
    "hook/SetProcessMitigationPolicy.cpp"
    "utils/Path.cpp"
    "utils/RemoteLog.cpp"
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
