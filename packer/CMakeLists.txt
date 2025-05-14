cmake_minimum_required(VERSION 3.15)
project(packer)

option(APPBOX_PACKER_SUFFIX "Suffix for executable" "")
set(appbox_packer_name ${PROJECT_NAME}${APPBOX_PACKER_SUFFIX})

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
# Setup executable
###############################################################################

set(loader_sources
    "main.cpp"
)

add_executable(${appbox_packer_name} WIN32 ${loader_sources})
appbox_setup_target_wall(${appbox_packer_name})

###############################################################################
# Dependency
###############################################################################

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party third_party)
target_link_libraries(${appbox_packer_name} PRIVATE nlohmann_json::nlohmann_json)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${appbox_packer_name})
