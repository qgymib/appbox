cmake_minimum_required(VERSION 3.15)
project(appbox)

option(APPBOX_BUILD_SANDBOX "Build sandbox" OFF)
option(APPBOX_BUILD_LOADER "Build loader" OFF)
option(APPBOX_BUILD_TESTS "Build tests" OFF)

# Add cmake search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

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
# Project products
###############################################################################

if (APPBOX_BUILD_SANDBOX OR APPBOX_BUILD_LOADER)
    add_subdirectory(common)
endif ()

if (APPBOX_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif ()

if (APPBOX_BUILD_LOADER)
    add_subdirectory(loader)
endif ()

###############################################################################
# Project products
###############################################################################

if (APPBOX_BUILD_TESTS)
    add_subdirectory(test)
endif ()
