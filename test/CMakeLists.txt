
add_executable(AppBoxTests
    "cases/__init__.cpp"
    "cases/ArugmentsPassthrough.cpp"
    "cases/OpenFile.cpp"
    "cases/ReadFile.cpp"
    "cases/RPC.cpp"
    "probe/__init__.cpp"
    "probe/Server.cpp"
    "utils/Semaphore.cpp"
    "Test.cpp"
    "main.cpp"
)
appbox_setup_target_wall(AppBoxTests)

target_include_directories(AppBoxTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

###############################################################################
# Dependency
###############################################################################
target_link_libraries(AppBoxTests PRIVATE AppBoxCommon)

find_package(base64 REQUIRED)
target_link_libraries(AppBoxTests PRIVATE base64)

find_package(CLI11 REQUIRED)
target_link_libraries(AppBoxTests PRIVATE CLI11::CLI11)

find_package(googletest REQUIRED)
target_link_libraries(AppBoxTests PRIVATE googletest::gtest)

find_package(nlohmann_json REQUIRED)
target_link_libraries(AppBoxTests PRIVATE nlohmann_json::nlohmann_json)

find_package(spdlog REQUIRED)
target_link_libraries(AppBoxTests PRIVATE spdlog::spdlog)
target_compile_options(AppBoxTests PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

###############################################################################
# Export test
###############################################################################
add_test(NAME AppBoxTests
    COMMAND AppBoxTests
        --loader=$<TARGET_FILE:AppBoxLoader>
)
