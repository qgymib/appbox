cmake_minimum_required(VERSION 3.15)
project(AppBoxLoader)

add_executable(${PROJECT_NAME} WIN32
    "rpc/__init__.cpp"
    "rpc/Log.cpp"
    "utils/CommandLineOptions.cpp"
    "utils/ConvertDosPathToNtPath.cpp"
    "utils/GetExecutableDir.cpp"
    "utils/KnownFolder.cpp"
    "utils/MapBaseFS.cpp"
    "utils/MiniLauncher.cpp"
    "utils/ProcessJob.cpp"
    "utils/WinCall.cpp"
    "widget/MainFrame.cpp"
    "Loader.cpp"
    "main.cpp"
    "resource.rc"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

###############################################################################
# Dependency
###############################################################################
if (NOT TARGET AppBoxCommon)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_BINARY_DIR}/common
    )
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE AppBoxCommon)

find_package(base64 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE base64)

find_package(CLI11 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

find_package(Detours REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_compile_options(${PROJECT_NAME} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

find_package(wxWidgets REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE wx::core wx::base)

###############################################################################
# Embed Sandbox
###############################################################################

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        sandbox_resource
)
