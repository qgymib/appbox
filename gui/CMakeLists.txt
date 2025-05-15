cmake_minimum_required(VERSION 3.15)
project(gui)

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

set(gui_sources
    "main.cpp"
    "resource.rc"
)

add_executable(${PROJECT_NAME} WIN32 ${gui_sources})
appbox_setup_target_wall(${PROJECT_NAME})

###############################################################################
# Dependency
###############################################################################

set(HAVE_JSON ON)
set(HAVE_WXWIDGETS ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party third_party)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        nlohmann_json::nlohmann_json
        wx::core
        wx::base
)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${PROJECT_NAME})
