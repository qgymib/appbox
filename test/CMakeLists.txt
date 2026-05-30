
add_executable(AppBoxTests
    "cases/ArugmentsPassthrough.cpp"
    "cases/Fs_DeleteFile_MultiLower_ExistsInLower.cpp"
    "cases/Fs_DeleteFile_MultiLower_ExistsInLowerUpper.cpp"
    "cases/Fs_DeleteFile_MultiLower_ExistsInUpper.cpp"
    "cases/Fs_DeleteFile_MultiLower_NonExists.cpp"
    "cases/Fs_DeleteFile_WhiteoutInLower_ExistsInUpper.cpp"
    "cases/Fs_NewFile_MultiLower_WhiteoutInLower.cpp"
    "cases/Fs_NewFile_MultiLower_WhiteoutInUpper.cpp"
    "cases/Fs_ReadFile_MultiLower_WhiteoutInUpper.cpp"
    "cases/RPC.cpp"
    "probe/__init__.cpp"
    "probe/CreateDirectoryW.cpp"
    "probe/CreateFileW.cpp"
    "probe/DeleteFileW.cpp"
    "probe/ReadFileFull.cpp"
    "utils/CommonFixture.cpp"
    "utils/CWD.cpp"
    "utils/FsBuilder.cpp"
    "utils/KnownFolder.cpp"
    "utils/ProbeCall.cpp"
    "utils/ReadFileFull.cpp"
    "utils/Semaphore.cpp"
    "utils/WriteFileFull.cpp"
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
