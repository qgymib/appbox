cmake_minimum_required(VERSION 3.15)
project(appbox)

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

add_subdirectory(common)
add_subdirectory(sandbox)
add_subdirectory(loader)
