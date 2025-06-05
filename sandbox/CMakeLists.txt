cmake_minimum_required(VERSION 3.15)
project(sandbox)

option(APPBOX_SANDBOX_SUFFIX "Suffix for library" "")
set(sandbox_name ${PROJECT_NAME}${APPBOX_SANDBOX_SUFFIX})

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
# Setup library
###############################################################################

set(appbox_sources
    "hooks/__init__.cpp"
    "hooks/CreateProcessInternalW.cpp"
    "hooks/NtCreateFile.cpp"
    "appbox.cpp"
    "appbox.def"
)

add_library(${sandbox_name} SHARED ${appbox_sources})
appbox_setup_target_wall(${sandbox_name})

target_include_directories(${sandbox_name}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

###############################################################################
# Dependency
###############################################################################

target_link_libraries(${sandbox_name} PRIVATE Detours)
target_link_libraries(${sandbox_name} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${sandbox_name} PRIVATE spdlog::spdlog)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${sandbox_name})
