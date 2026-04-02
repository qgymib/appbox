cmake_minimum_required(VERSION 3.15)
project(loader)

set(loader_name ${PROJECT_NAME})

add_executable(${loader_name}
    "main.cpp"
)
appbox_setup_target_wall(${loader_name})
target_link_libraries(${loader_name} PRIVATE common)

find_package(CLI11 REQUIRED)
target_link_libraries(${loader_name} PRIVATE CLI11::CLI11)

find_package(Detours REQUIRED)
target_link_libraries(${loader_name} PRIVATE Detours)

find_package(spdlog REQUIRED)
target_link_libraries(${loader_name} PRIVATE spdlog::spdlog)
target_compile_options(${loader_name} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
