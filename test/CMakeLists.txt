
add_executable(AppBoxTests
    "cases/ArugmentsPassthrough.cpp"
    "cases/Fs_DeleteFile_MultiLower_ExistsInLower.cpp"
    "cases/Fs_DeleteFile_MultiLower_ExistsInLowerUpper.cpp"
    "cases/Fs_DeleteFile_MultiLower_ExistsInUpper.cpp"
    "cases/Fs_DeleteFile_MultiLower_NonExists.cpp"
    "cases/Fs_DeleteFile_WhiteoutInLower_ExistsInUpper.cpp"
    "cases/Fs_LaunchProcess_FromLower.cpp"
    "cases/Fs_ListDir_MultiLower_ExistsInLower_WhiteoutInUpper.cpp"
    "cases/Fs_ListDir_MultiLower_ExistsInLower.cpp"
    "cases/Fs_NewFile_MultiLower_WhiteoutInLower.cpp"
    "cases/Fs_NewFile_MultiLower_WhiteoutInUpper.cpp"
    "cases/Fs_QueryAttributes_MultiLower.cpp"
    "cases/Fs_ReadFile_MultiLower_WhiteoutInUpper.cpp"
    "cases/Reg_EnumerateKey_Merged.cpp"
    "cases/Reg_EnumerateValue_Merged.cpp"
    "cases/Reg_QueryKeyName_ViewPath.cpp"
    "cases/Reg_ReadValue_RealFallback.cpp"
    "cases/Reg_ShadowKey_ReadRealValue.cpp"
    "cases/Reg_WriteValue_NewKey.cpp"
    "cases/RPC.cpp"
    "probe/__init__.cpp"
    "probe/CreateDirectoryW.cpp"
    "probe/CreateFileW.cpp"
    "probe/DeleteFileW.cpp"
    "probe/LaunchProcess.cpp"
    "probe/ListDir.cpp"
    "probe/QueryAttributes.cpp"
    "probe/ReadFileFull.cpp"
    "probe/RegEnumKey.cpp"
    "probe/RegEnumValue.cpp"
    "probe/RegQueryKeyName.cpp"
    "probe/RegReadValue.cpp"
    "probe/RegShadowRead.cpp"
    "probe/RegWriteValue.cpp"
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

###############################################################################
# Unit tests
#
# Pure in-process tests. They do not start the loader nor inject the sandbox
# DLL, so they can run without the end-to-end probe chain of AppBoxTests.
###############################################################################
add_executable(AppBoxUnitTests
    "unit/main.cpp"
    "unit/Unit_ApplicationIcon.cpp"
    "unit/Unit_BuildReport.cpp"
    "unit/Unit_HiveReader.cpp"
    "unit/Unit_HookTransaction.cpp"
    "unit/Unit_ExpandKnownFolder.cpp"
    "unit/Unit_GetExecutableDir.cpp"
    "unit/Unit_LoaderPath.cpp"
    "unit/Unit_Log.cpp"
    "unit/Unit_MiniLauncher.cpp"
    "unit/Unit_ModuleTable.cpp"
    "unit/Unit_PackModel.cpp"
    "unit/Unit_PackService.cpp"
    "unit/Unit_ProcessJob.cpp"
    "unit/Unit_RegistryEnumMerge.cpp"
    "unit/Unit_RegistryKeyPath.cpp"
    "unit/Unit_RemoteClient.cpp"
    "unit/Unit_RpcCodec.cpp"
    "unit/Unit_StartupTree.cpp"
    "unit/Unit_WString.cpp"
    "unit/Unit_ZipReader.cpp"
    "unit/Unit_ZipWriter.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/registry/HiveReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/ConvertDosPathToNtPath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/GetExecutableDir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/KnownFolder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/MapBaseFS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/MiniLauncher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/ProcessJob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../loader/utils/WinCall.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ApplicationIcon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/BuildReport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/PackModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/PackService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/PresetDirectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/StartupTree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ZipReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ZipWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/ModuleTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/hook/HookTransaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/EnumMerge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/KeyPath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/BitParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/Log.cpp
)
appbox_setup_target_wall(AppBoxUnitTests)

# The project directories come first: test/utils holds headers with the same
# name as the loader ones, and the loader headers have to win.
target_include_directories(AppBoxUnitTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../loader
        ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AppBoxUnitTests PRIVATE AppBoxCommon)

find_package(CLI11 REQUIRED)
target_link_libraries(AppBoxUnitTests PRIVATE CLI11::CLI11)

find_package(googletest REQUIRED)
target_link_libraries(AppBoxUnitTests PRIVATE googletest::gtest)

find_package(nlohmann_json REQUIRED)
target_link_libraries(AppBoxUnitTests PRIVATE nlohmann_json::nlohmann_json)

find_package(spdlog REQUIRED)
target_link_libraries(AppBoxUnitTests PRIVATE spdlog::spdlog)

find_package(Detours REQUIRED)
target_link_libraries(AppBoxUnitTests PRIVATE Detours)

# Used by the known folder helper of the loader.
target_link_libraries(AppBoxUnitTests PRIVATE shell32 ole32)

# Registry access of the sandbox hive reader (RegLoadAppKeyW and friends).
target_link_libraries(AppBoxUnitTests PRIVATE advapi32)

# Zip archive creation of the packer core (zlib must be found first so the
# libzip build reuses the already configured target).
find_package(ZLIB REQUIRED)
find_package(libzip REQUIRED)
target_link_libraries(AppBoxUnitTests PRIVATE libzip::zip)

# The unit tests which work on the real loader payload are given the path of
# the loader executable; the remaining tests do not need it.
add_dependencies(AppBoxUnitTests AppBoxLoader)

add_test(NAME AppBoxUnitTests
    COMMAND AppBoxUnitTests
        --loader=$<TARGET_FILE:AppBoxLoader>
)
