###############################################################################
# The test executable
#
# One executable runs both sides of the test suite of the project: the
# in-process unit tests below `test/unit` and the end-to-end cases below
# `test/e2e`, which start the real launcher and let it inject the sandbox DLL
# into this executable started as the probe process of the case.
#
# The `--mode` option selects the side of a run; CTest registers one entry per
# mode, so the unit tests still run on their own in a second.
#
# The source files are an explicit list, not a glob, so a new test file has to
# be registered here.
#
# The include directories put the project directories first: `test/utils` holds
# headers with the same name as the launcher ones and the launcher headers have to
# win. The known folder helper of the cases is named `TestKnownFolder` for that
# reason.
###############################################################################
add_executable(AppBoxTests
    # Unit tests. They run in the process of the test executable and do not
    # start the launcher, so they are listed first and run first.
    "unit/AboutInfo.cpp"
    "unit/ApplicationIcon.cpp"
    "unit/ApplicationMetadata.cpp"
    "unit/BuildReport.cpp"
    "unit/EnvironmentIsolation.cpp"
    "unit/EnvironmentIsolationFile.cpp"
    "unit/EnvironmentModel.cpp"
    "unit/EmbeddedResource.cpp"
    "unit/EnvironmentTable.cpp"
    "unit/ExpandKnownFolder.cpp"
    "unit/FilesystemIsolation.cpp"
    "unit/GetExecutableDir.cpp"
    "unit/HiveMerge.cpp"
    "unit/HookTransaction.cpp"
    "unit/IsolationDocument.cpp"
    "unit/Log.cpp"
    "unit/LogFile.cpp"
    "unit/Md5.cpp"
    "unit/ProcessLauncher.cpp"
    "unit/ModuleTable.cpp"
    "unit/NetworkIsolationFile.cpp"
    "unit/NetworkModel.cpp"
    "unit/PackModel.cpp"
    "unit/PackService.cpp"
    "unit/PipeClient.cpp"
    "unit/ProcessJob.cpp"
    "unit/ProjectDocument.cpp"
    "unit/ProjectFile.cpp"
    "unit/ProjectType.cpp"
    "unit/ProxyConfig.cpp"
    "unit/RegFile.cpp"
    "unit/RegistryHive.cpp"
    "unit/RegistryModel.cpp"
    "unit/RemoteClient.cpp"
    "unit/RpcCodec.cpp"
    "unit/Shell.cpp"
    "unit/Socks5.cpp"
    "unit/StartupTree.cpp"
    "unit/StaticRuntime.cpp"
    "unit/TestTimeout.cpp"
    "unit/TracerArmPlan.cpp"
    "unit/TracerCdbLocator.cpp"
    "unit/TracerCdbOutput.cpp"
    "unit/TracerIntegration.cpp"
    "unit/TracerModel.cpp"
    "unit/TracerPeImage.cpp"
    "unit/TracerScope.cpp"
    "unit/TracerTargetProgram.cpp"
    "unit/VariableExpansion.cpp"
    "unit/WString.cpp"
    "unit/ZipReader.cpp"
    "unit/ZipWriter.cpp"

    # End-to-end cases.
    "e2e/Env_Full_HidesTheHostValue.cpp"
    "e2e/Env_MalformedIsolationFile_FallsBack.cpp"
    "e2e/Env_MissingHostValue_KeepsTheConfiguredValue.cpp"
    "e2e/Env_ModificationStaysInTheSandbox.cpp"
    "e2e/Env_StateFileIsApplied.cpp"
    "e2e/Env_StateIsKeptAcrossRuns.cpp"
    "e2e/Env_VariableExpansion.cpp"
    "e2e/Env_WriteCopy_Append.cpp"
    "e2e/Env_WriteCopy_Host.cpp"
    "e2e/Env_WriteCopy_Prepend.cpp"
    "e2e/Env_WriteCopy_Replace.cpp"
    "e2e/Font_AddFontResourceEx_ViewPath.cpp"
    "e2e/Font_HostIsNotModified.cpp"
    "e2e/Font_PackedFontIsUsable.cpp"
    "e2e/Fs_DefaultMerge_CreatesTheEntriesInTheHost.cpp"
    "e2e/Fs_DefaultMerge_WritesTheFileOfTheHost.cpp"
    "e2e/Fs_DeleteFile_LowerLayer.cpp"
    "e2e/Fs_DeleteFile_LowerLayerAndUpper.cpp"
    "e2e/Fs_DeleteFile_NonExists.cpp"
    "e2e/Fs_DeleteFile_UpperOnly.cpp"
    "e2e/Fs_DeleteFile_WhiteoutInLower_ExistsInUpper.cpp"
    "e2e/Fs_DeleteOnClose_LowerLayer.cpp"
    "e2e/Fs_DeleteOnClose_MergeHost.cpp"
    "e2e/Fs_DeleteOnClose_UpperOnly.cpp"
    "e2e/Fs_DeleteOnClose_WhiteoutInUpper.cpp"
    "e2e/Fs_DirectoryOperations.cpp"
    "e2e/Fs_Full_HidesHostFolder.cpp"
    "e2e/Fs_Full_SubFolderWriteCopy.cpp"
    "e2e/Fs_LaunchProcess_FromLower.cpp"
    "e2e/Fs_Link.cpp"
    "e2e/Fs_ListDir_CreateFileHandle.cpp"
    "e2e/Fs_ListDir_DuplicatedHandle.cpp"
    "e2e/Fs_ListDir_InformationClasses.cpp"
    "e2e/Fs_ListDir_IsolationFiltered.cpp"
    "e2e/Fs_ListDir_LowerLayer.cpp"
    "e2e/Fs_ListDir_UnregisteredNotInView.cpp"
    "e2e/Fs_ListDir_UnsupportedClass.cpp"
    "e2e/Fs_ListDir_UserPresetLayers.cpp"
    "e2e/Fs_ListDir_WhiteoutInUpper.cpp"
    "e2e/Fs_MalformedIsolationFile_FallsBack.cpp"
    "e2e/Fs_Merge_CreatesTheFoldersOfANewFile.cpp"
    "e2e/Fs_Merge_DeletesTheHostFile.cpp"
    "e2e/Fs_Merge_DeletesThePackedFile.cpp"
    "e2e/Fs_Merge_KeepsThePackedFileInTheSandbox.cpp"
    "e2e/Fs_Merge_WritesTheHostFile.cpp"
    "e2e/Fs_NewFile_WhiteoutInLower.cpp"
    "e2e/Fs_NewFile_WhiteoutInUpper.cpp"
    "e2e/Fs_QueryAttributes_LowerLayer.cpp"
    "e2e/Fs_QueryAttributes_ProgramPresetLayers.cpp"
    "e2e/Fs_QueryAttributes_SystemPresetLayers.cpp"
    "e2e/Fs_QueryFullAttributes.cpp"
    "e2e/Fs_QueryInformationByName.cpp"
    "e2e/Fs_QueryInformationFile.cpp"
    "e2e/Fs_ReadFile_WhiteoutInUpper.cpp"
    "e2e/Fs_Rename.cpp"
    "e2e/Fs_RootIsolation_MergeWritesTheHostFile.cpp"
    "e2e/Fs_Whiteout_CreateInSandbox.cpp"
    "e2e/Fs_Whiteout_FileNotFound.cpp"
    "e2e/Fs_Whiteout_FolderHidesSubtree.cpp"
    "e2e/Fs_WriteLowerLayerFile_CopyUp.cpp"
    "e2e/Launcher_HideConsole.cpp"
    "e2e/Launcher_MissingSandboxDll.cpp"
    "e2e/Launcher_NoVcRuntime.cpp"
    "e2e/Launcher_RegistryStateIsKept.cpp"
    "e2e/Launcher_RegistryStateIsReset.cpp"
    "e2e/Launcher_RegistryStateIsSeeded.cpp"
    "e2e/Launcher_SandboxDllFromTheApp.cpp"
    "e2e/Launcher_Shell.cpp"
    "e2e/Launcher_Startup.cpp"
    "e2e/Net_Dns_EveryResolutionApiIsRedirected.cpp"
    "e2e/Net_Dns_FamilyOfTheRedirectIsHonoured.cpp"
    "e2e/Net_Dns_HostnameIsNormalized.cpp"
    "e2e/Net_Dns_MissIsResolvedByTheHost.cpp"
    "e2e/Net_Dns_RedirectIsReturned.cpp"
    "e2e/Net_Proxy_CredentialsAreSent.cpp"
    "e2e/Net_Proxy_DisabledTrafficKeepsTheDirectPath.cpp"
    "e2e/Net_Proxy_MalformedConfigurationFallsBack.cpp"
    "e2e/Net_Proxy_NonBlockingSocketIsProxied.cpp"
    "e2e/Net_Proxy_TcpConnectIsCarriedByTheProxy.cpp"
    "e2e/Net_Proxy_UdpDatagramIsCarriedByTheProxy.cpp"
    "e2e/Net_Proxy_UnreachableServerFailsTheConnect.cpp"
    "e2e/Patch_BrokenEnvironmentResourcesAreSkipped.cpp"
    "e2e/Patch_BrokenNetworkResourcesAreSkipped.cpp"
    "e2e/Patch_BrokenPackageIsSkipped.cpp"
    "e2e/Patch_BrokenRegistryResourcesAreSkipped.cpp"
    "e2e/Patch_CacheIsReusedAndRefreshed.cpp"
    "e2e/Patch_ContentOverridesTheApp.cpp"
    "e2e/Patch_EnvironmentFullDropsTheLayersBelow.cpp"
    "e2e/Patch_EnvironmentHostPassesBelowThrough.cpp"
    "e2e/Patch_EnvironmentLayersComposeInOrder.cpp"
    "e2e/Patch_FontOfTheLastLayerWins.cpp"
    "e2e/Patch_IsolationModeOfTheLastLayerWins.cpp"
    "e2e/Patch_MergeOverridesTheArchive.cpp"
    "e2e/Patch_NetworkDnsOfTheLastLayerWins.cpp"
    "e2e/Patch_NetworkProxyBelowIsKept.cpp"
    "e2e/Patch_NetworkProxyOfTheLastLayerWins.cpp"
    "e2e/Patch_NoPackageKeepsTheCacheEmpty.cpp"
    "e2e/Patch_RegistryHiveOfTheLastLayerWins.cpp"
    "e2e/Patch_RegistryIsolationModeOfTheLastLayerWins.cpp"
    "e2e/Patch_RegistryStateBelowThePatchIsKept.cpp"
    "e2e/Patch_RegistryWithoutResourcesKeepsTheApp.cpp"
    "e2e/Reg_DeleteKey_NonEmpty.cpp"
    "e2e/Reg_DeleteKey_ReadHandle.cpp"
    "e2e/Reg_DeleteKey_ShadowKeyRemoved.cpp"
    "e2e/Reg_DeleteKey_WhiteoutHostKey.cpp"
    "e2e/Reg_DeleteValue_ShadowValue.cpp"
    "e2e/Reg_DeleteValue_WhiteoutHostValue.cpp"
    "e2e/Reg_EnumerateKey_Merged.cpp"
    "e2e/Reg_EnumerateValue_DefaultValue.cpp"
    "e2e/Reg_EnumerateValue_Merged.cpp"
    "e2e/Reg_Full_EnumerateHidesReal.cpp"
    "e2e/Reg_Full_HidesRealKey.cpp"
    "e2e/Reg_Full_HidesRealValue.cpp"
    "e2e/Reg_Full_HiveWinsOverReal.cpp"
    "e2e/Reg_Full_NonHkcuRoot.cpp"
    "e2e/Reg_IsolationInheritance.cpp"
    "e2e/Reg_MalformedIsolationFile_FallsBack.cpp"
    "e2e/Reg_Hide_CreateInSandbox.cpp"
    "e2e/Reg_QueryKeyName_ViewPath.cpp"
    "e2e/Reg_QueryMultipleValues_Mixed.cpp"
    "e2e/Reg_ReadValue_RealFallback.cpp"
    "e2e/Reg_SaveKey_MergedSnapshot.cpp"
    "e2e/Reg_ShadowKey_HidesRealEntriesOfSameName.cpp"
    "e2e/Reg_ShadowKey_ReadRealValue.cpp"
    "e2e/Reg_UsersRoot_MapsToTheCurrentUserHive.cpp"
    "e2e/Reg_VariableExpansion.cpp"
    "e2e/Reg_WriteCopy_CreateDisposition.cpp"
    "e2e/Reg_WriteCopy_WriteOpen_CopyUp.cpp"
    "e2e/Reg_WriteValue_NewKey.cpp"

    # The operations which are executed inside the sandbox. The launcher starts
    # this executable as the probe process of a case.
    "probe/__init__.cpp"
    "probe/ConsoleWindow.cpp"
    "probe/CreateDirectoryW.cpp"
    "probe/CreateFileW.cpp"
    "probe/DeleteFileW.cpp"
    "probe/EnvironmentRead.cpp"
    "probe/EnvironmentWrite.cpp"
    "probe/Fonts.cpp"
    "probe/LaunchProcess.cpp"
    "probe/ListDir.cpp"
    "probe/ListDirNt.cpp"
    "probe/LoadedModules.cpp"
    "probe/QueryAttributes.cpp"
    "probe/QueryFullAttributes.cpp"
    "probe/QueryInformationByName.cpp"
    "probe/QueryInformationFile.cpp"
    "probe/RemoveDirectory.cpp"
    "probe/ReadFileFull.cpp"
    "probe/RegDeleteKey.cpp"
    "probe/RegDeleteValue.cpp"
    "probe/RegEnumKey.cpp"
    "probe/RegEnumValue.cpp"
    "probe/RegOpenWriteValue.cpp"
    "probe/RegQueryKeyName.cpp"
    "probe/RegQueryMultipleValues.cpp"
    "probe/RegReadValue.cpp"
    "probe/RegReadValues.cpp"
    "probe/RegSaveKey.cpp"
    "probe/RegShadowRead.cpp"
    "probe/RegWriteValue.cpp"
    "probe/WriteFile.cpp"
    "probe/ResolveName.cpp"
    "probe/SetInformationFile.cpp"
    "probe/SocketTraffic.cpp"
    "probe/StartupStarted.cpp"

    # Test helpers.
    "utils/CommandLine.cpp"
    "utils/CommonFixture.cpp"
    "utils/Coredump.cpp"
    "utils/CWD.cpp"
    "utils/EchoServer.cpp"
    "utils/EnvironmentIsolationBuilder.cpp"
    "utils/FsBuilder.cpp"
    "utils/FsIsolationBuilder.cpp"
    "utils/HiveBuilder.cpp"
    "utils/NameResolutionProbe.cpp"
    "utils/NetworkIsolationBuilder.cpp"
    "utils/PatchBuilder.cpp"
    "utils/ProbeCall.cpp"
    "utils/ReadFileFull.cpp"
    "utils/RealFsFolder.cpp"
    "utils/RealHkcuKey.cpp"
    "utils/RegistryRootKey.cpp"
    "utils/Semaphore.cpp"
    "utils/Socks5Server.cpp"
    "utils/TestFont.cpp"
    "utils/TestKnownFolder.cpp"
    "utils/TestLog.cpp"
    "utils/TestTimeout.cpp"
    "utils/WriteFileFull.cpp"

    # Configuration and entry point.
    "Test.cpp"
    "main.cpp"

    # Code under test. The unit tests work on the modules directly, so they are
    # compiled into the test executable instead of being linked from a library.
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/ConvertDosPathToNtPath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/GetExecutableDir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/HiveMerge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/KnownFolder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/MapBaseFS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/Md5.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/ProcessJob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/ProcessLauncher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/Shell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../launcher/utils/WinCall.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/AboutInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ApplicationIcon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ApplicationMetadata.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/BuildReport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/EmbeddedResource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/EnvironmentIsolationFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/EnvironmentModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/FilesystemIsolationFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/FilesystemIsolationModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/NetworkIsolationFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/NetworkModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/PackModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/PackService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/PresetDirectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ProjectDocument.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ProjectFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ProjectType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RegFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RegistryHive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RegistryIsolationFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/RegistryModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/StartupTree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ZipReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/ZipWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/ModuleTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/environment/Configuration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/environment/Table.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/filesystem/IsolationTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/network/DnsTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/network/ProxyConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/network/Socks5.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/EnumMerge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/IsolationTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/KeyPath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/RootMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/registry/Whiteout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/BitParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/CrashReport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/HookTransaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/Log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/LogFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/PipeClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox/utils/VariableExpansion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/ArmPlan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/Category.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/CdbLocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/CdbOutputParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/CdbSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/PeImage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/ScopePatterns.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/TargetProgram.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracer/TracedModules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/TracerModel.cpp
)
appbox_setup_target_wall(AppBoxTests)

target_include_directories(AppBoxTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../launcher
        ${CMAKE_CURRENT_SOURCE_DIR}/../sandbox
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${APPBOX_GENERATED_DIR}
)

# The build information header has to exist before the sources are compiled,
# and the launcher has to be built: the end-to-end cases start it.
add_dependencies(AppBoxTests appbox_build_info)
add_dependencies(AppBoxTests AppBoxLauncher)

# The end-to-end cases start the launcher, which injects the modules below
# `resource/lib`, and the unit test of the embedded resources reads the
# resources of the packer.
add_dependencies(AppBoxTests
    AppBox
    sandbox_x86_build
    sandbox_x64_build
)

###############################################################################
# Dependency
###############################################################################
target_link_libraries(AppBoxTests PRIVATE AppBoxCommon)

find_package(base64 REQUIRED)
target_link_libraries(AppBoxTests PRIVATE base64)

# The probe of the name resolution calls the DNS client of the system.
target_link_libraries(AppBoxTests PRIVATE dnsapi)

# The font probe and the helper which builds a test font use the font API of the
# system (EnumFontFamiliesExW, CreateFontIndirectW, AddFontResourceExW).
target_link_libraries(AppBoxTests PRIVATE gdi32)

find_package(CLI11 REQUIRED)
target_link_libraries(AppBoxTests PRIVATE CLI11::CLI11)

find_package(googletest REQUIRED)
target_link_libraries(AppBoxTests PRIVATE googletest::gtest)

find_package(nlohmann_json REQUIRED)
target_link_libraries(AppBoxTests PRIVATE nlohmann_json::nlohmann_json)

find_package(spdlog REQUIRED)
target_link_libraries(AppBoxTests PRIVATE spdlog::spdlog)
target_compile_options(AppBoxTests PRIVATE -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# The unit tests work on the hooks of the sandbox, which are attached with the
# transaction helpers of Detours.
find_package(Detours REQUIRED)
target_link_libraries(AppBoxTests PRIVATE Detours)

# Used by the known folder helper of the launcher.
target_link_libraries(AppBoxTests PRIVATE shell32 ole32)

# Registry access of the sandbox hive reader (RegLoadAppKeyW and friends).
target_link_libraries(AppBoxTests PRIVATE advapi32)

# Version resource of the packer core, which reads the program information the
# launcher payload inherits from the main program of the packaged application
# (GetFileVersionInfoW and VerQueryValueW).
target_link_libraries(AppBoxTests PRIVATE version)

# Digest of a patch package (BCryptOpenAlgorithmProvider and friends).
target_link_libraries(AppBoxTests PRIVATE bcrypt)

# The coredump of a timed out test case is written with MiniDumpWriteDump.
target_link_libraries(AppBoxTests PRIVATE dbghelp)

# Zip archive creation of the packer core (zlib must be found first so the
# libzip build reuses the already configured target).
find_package(ZLIB REQUIRED)
find_package(libzip REQUIRED)
target_link_libraries(AppBoxTests PRIVATE libzip::zip)

###############################################################################
# Test entries
#
# Both entries run the same executable with a different mode, so the unit tests
# stay a run of their own which does not start the launcher.
###############################################################################
add_test(NAME AppBoxUnitTests
    COMMAND AppBoxTests
        --launcher=$<TARGET_FILE:AppBoxLauncher>
        --sandbox32=${APPBOX_RESOURCE_SANDBOX32}
        --sandbox64=${APPBOX_RESOURCE_SANDBOX64}
        --packer=$<TARGET_FILE:AppBox>
        --mode=unit
)

add_test(NAME AppBoxTests
    COMMAND AppBoxTests
        --launcher=$<TARGET_FILE:AppBoxLauncher>
        --sandbox32=${APPBOX_RESOURCE_SANDBOX32}
        --sandbox64=${APPBOX_RESOURCE_SANDBOX64}
        --packer=$<TARGET_FILE:AppBox>
        --mode=e2e
)
