cmake_minimum_required(VERSION 3.15)
project(common)

add_library(${PROJECT_NAME} STATIC
    "CRC32.cpp"
    "InjectData.cpp"
    "PipeServer.cpp"
    "WString.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(nlohmann_json REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC nlohmann_json::nlohmann_json)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_compile_options(${PROJECT_NAME} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

find_package(asio REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE asio)
