cmake_minimum_required(VERSION 3.15)
project(AppBoxSandbox)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(APPBOX_SANDBOX_SUFFIX 64)
else ()
    set(APPBOX_SANDBOX_SUFFIX 32)
endif ()

set(sandbox_name ${PROJECT_NAME}${APPBOX_SANDBOX_SUFFIX})

add_library(${sandbox_name} SHARED
    "hook/__init__.cpp"
    "hook/CreateProcessInternalW.cpp"
    "hook/LdrQueryImageFileExecutionOptionsEx.cpp"
    "hook/SetProcessMitigationPolicy.cpp"
    "utils/RemoteLog.cpp"
    "utils/Task.cpp"
    "Sandbox.cpp"
    "Sandbox.def"
)

appbox_setup_target_wall(${sandbox_name})
set_target_properties(${sandbox_name}
    PROPERTIES
        CXX_STANDARD 17
        PREFIX ""
)

target_include_directories(${sandbox_name}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

###############################################################################
# Dependency
###############################################################################
target_link_libraries(${sandbox_name} PRIVATE AppBoxCommon)

find_package(Detours REQUIRED)
target_link_libraries(${sandbox_name} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${sandbox_name} PRIVATE spdlog::spdlog)
target_compile_definitions(${sandbox_name} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
