cmake_minimum_required(VERSION 3.15)
project(packer)

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

set(packer_sources
    "widgets/FileDataView.cpp"
    "widgets/FilePanel.cpp"
    "widgets/LogPanel.cpp"
    "widgets/MainFrame.cpp"
    "widgets/ProcessDialog.cpp"
    "widgets/RegistryPanel.cpp"
    "widgets/SettingsPanel.cpp"
    "widgets/StartupFilesDialog.cpp"
    "App.cpp"
    "resource.rc"
)

add_executable(${PROJECT_NAME} WIN32 ${packer_sources})
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${PROJECT_NAME} PRIVATE Pathcch)

###############################################################################
# Dependency
###############################################################################

target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE wx::core wx::base)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIBSTATIC)

###############################################################################
# Install target
###############################################################################

install(TARGETS ${PROJECT_NAME})
