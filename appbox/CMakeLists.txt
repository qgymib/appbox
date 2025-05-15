cmake_minimum_required(VERSION 3.15)
project(appbox)

option(APPBOX_DLL_SUFFIX "Suffix for library" "")
set(appbox_lib_name ${PROJECT_NAME}${APPBOX_DLL_SUFFIX})

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
    "hooks/CreateFileA.cpp"
    "hooks/CreateFileW.cpp"
    "hooks/LoadLibraryA.cpp"
    "hooks/LoadLibraryW.cpp"
    "appbox.cpp"
)

add_library(${appbox_lib_name} SHARED ${appbox_sources})
appbox_setup_target_wall(${appbox_lib_name})

###############################################################################
# Dependency
###############################################################################

set(HAVE_DETOURS ON)
set(HAVE_ZLIB ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party third_party)
target_link_libraries(${appbox_lib_name} PRIVATE Detours)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils utils)
target_link_libraries(${appbox_lib_name} PRIVATE utils)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${appbox_lib_name})
