cmake_minimum_required(VERSION 3.15)
project(AppBoxLoader)

add_executable(${PROJECT_NAME}
    "rpc/__init__.cpp"
    "rpc/Log.cpp"
    "Loader.cpp"
    "main.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

###############################################################################
# Dependency
###############################################################################
target_link_libraries(${PROJECT_NAME} PRIVATE AppBoxCommon)

find_package(CLI11 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

find_package(Detours REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_compile_options(${PROJECT_NAME} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
