cmake_minimum_required(VERSION 3.15)
project(utils)

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

set(utils_sources
    "wstring.cpp"
)

add_library(${PROJECT_NAME} ${utils_sources})
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
