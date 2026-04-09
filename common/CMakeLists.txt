cmake_minimum_required(VERSION 3.15)
project(AppBoxCommon)

###############################################################################
# Export library
###############################################################################
add_library(${PROJECT_NAME} STATIC
    "CRC32.cpp"
    "InjectData.cpp"
    "RemoteClient.cpp"
    "RemoteServer.cpp"
    "RemoteSession.cpp"
    "WString.cpp"
)
appbox_setup_target_wall(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        CXX_STANDARD 17
)

###############################################################################
# Dependency
###############################################################################
find_package(asio REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE asio ws2_32)

find_package(expected REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC tl::expected)

find_package(nlohmann_json REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC nlohmann_json::nlohmann_json)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_compile_options(${PROJECT_NAME} PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
