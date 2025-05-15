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

option(WXWIDGETS_SEARCH_PATH "Search path for wxWidgets" OFF)
if (WXWIDGETS_SEARCH_PATH)
    find_package(wxWidgets REQUIRED COMPONENTS core base PATHS ${WXWIDGETS_SEARCH_PATH} NO_DEFAULT_PATH)
else()
    find_package(wxWidgets REQUIRED COMPONENTS core base)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE wx::core wx::base)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${PROJECT_NAME})
