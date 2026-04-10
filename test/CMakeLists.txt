
add_executable(AppBoxTests
    "cases/ArugmentsPassthrough.cpp"
    "cases/RPC.cpp"
    "probe/__init__.cpp"
    "probe/HelloWorld.cpp"
    "Test.cpp"
    "main.cpp"
)
appbox_setup_target_wall(AppBoxTests)

target_include_directories(AppBoxTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

###############################################################################
# Dependency
###############################################################################
target_link_libraries(AppBoxTests PRIVATE AppBoxCommon)

find_package(CLI11 REQUIRED)
target_link_libraries(AppBoxTests PRIVATE CLI11::CLI11)

find_package(googletest REQUIRED)
target_link_libraries(AppBoxTests PRIVATE googletest::gtest)

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
