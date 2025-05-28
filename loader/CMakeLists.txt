cmake_minimum_required(VERSION 3.15)
project(loader)

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
    "supervise/__init__.cpp"
    "supervise/Path.cpp"
    "supervise/VariableDecoder.cpp"
    "widgets/LogPanel.cpp"
    "widgets/MainFrame.cpp"
    "App.cpp"
    "resource.rc"
)

add_executable(${PROJECT_NAME} WIN32 ${loader_sources})
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

###############################################################################
# Dependency
###############################################################################

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Detours
)

target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE wx::core wx::base)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIBSTATIC)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${PROJECT_NAME})
