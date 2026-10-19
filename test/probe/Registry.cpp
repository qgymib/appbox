#include "probe/ConsoleWindow.hpp"
#include "probe/CreateDirectoryW.hpp"
#include "probe/CreateFileW.hpp"
#include "probe/DeleteFileW.hpp"
#include "probe/EnvironmentRead.hpp"
#include "probe/EnvironmentWrite.hpp"
#include "probe/FindStreams.hpp"
#include "probe/Fonts.hpp"
#include "probe/LaunchProcess.hpp"
#include "probe/ListDir.hpp"
#include "probe/ListDirNt.hpp"
#include "probe/LoadedModules.hpp"
#include "probe/QueryAttributes.hpp"
#include "probe/QueryFullAttributes.hpp"
#include "probe/QueryInformationByName.hpp"
#include "probe/QueryInformationFile.hpp"
#include "probe/ReadFileFull.hpp"
#include "probe/RegDeleteKey.hpp"
#include "probe/RegDeleteValue.hpp"
#include "probe/RegEnumKey.hpp"
#include "probe/RegEnumValue.hpp"
#include "probe/RegKeyApi.hpp"
#include "probe/RegOpenWriteValue.hpp"
#include "probe/RegQueryKeyName.hpp"
#include "probe/RegQueryMultipleValues.hpp"
#include "probe/RegReadValue.hpp"
#include "probe/RegReadValues.hpp"
#include "probe/RegRenameKey.hpp"
#include "probe/RegSaveKey.hpp"
#include "probe/RegShadowRead.hpp"
#include "probe/RegTransacted.hpp"
#include "probe/RegTwoProcesses.hpp"
#include "probe/RegWriteValue.hpp"
#include "probe/RemoveDirectory.hpp"
#include "probe/ReparsePoint.hpp"
#include "probe/ResolveName.hpp"
#include "probe/SetInformationFile.hpp"
#include "probe/SocketTraffic.hpp"
#include "probe/StartupStarted.hpp"
#include "probe/WriteFile.hpp"

/*
 * The probe objects of the test executable.
 *
 * A probe is implemented by the probe executable (`test/probe/CMakeLists.txt`),
 * which defines the same objects together with the function that does the work.
 * The test executable only needs the name: `Probe::Call()` asks the probe
 * process for the work over the named pipe of the case, so the objects below
 * carry no function and register nothing.
 *
 * The names have to agree with the probe executable, which registers every
 * function under its name. A name which disagrees makes the probe answer that
 * it does not exist, so the case which calls it fails instead of passing
 * silently; a probe which is added to the probe executable is added here as
 * well, which a case which calls it enforces at link time.
 */
appbox::test::Probe appbox::test::ProbeConsoleWindow("ConsoleWindow");
appbox::test::Probe appbox::test::ProbeCreateDirectoryW("CreateDirectoryW");
appbox::test::Probe appbox::test::ProbeCreateFileW("CreateFileW");
appbox::test::Probe appbox::test::ProbeDeleteFileW("DeleteFileW");
appbox::test::Probe appbox::test::ProbeEnvironmentRead("EnvironmentRead");
appbox::test::Probe appbox::test::ProbeEnvironmentWrite("EnvironmentWrite");
appbox::test::Probe appbox::test::ProbeFindStreams("FindStreams");
appbox::test::Probe appbox::test::ProbeFonts("Fonts");
appbox::test::Probe appbox::test::ProbeLaunchProcess("LaunchProcess");
appbox::test::Probe appbox::test::ProbeListDir("ListDir");
appbox::test::Probe appbox::test::ProbeListDirNt("ListDirNt");
appbox::test::Probe appbox::test::ProbeLoadedModules("LoadedModules");
appbox::test::Probe appbox::test::ProbeQueryAttributes("QueryAttributes");
appbox::test::Probe appbox::test::ProbeQueryFullAttributes("QueryFullAttributes");
appbox::test::Probe appbox::test::ProbeQueryInformationByName("QueryInformationByName");
appbox::test::Probe appbox::test::ProbeQueryInformationFile("QueryInformationFile");
appbox::test::Probe appbox::test::ProbeReadFileFull("ReadFile");
appbox::test::Probe appbox::test::ProbeRegDeleteKey("RegDeleteKey");
appbox::test::Probe appbox::test::ProbeRegDeleteValue("RegDeleteValue");
appbox::test::Probe appbox::test::ProbeRegEnumKey("RegEnumKey");
appbox::test::Probe appbox::test::ProbeRegEnumValue("RegEnumValue");
appbox::test::Probe appbox::test::ProbeRegKeyApi("RegKeyApi");
appbox::test::Probe appbox::test::ProbeRegOpenWriteValue("RegOpenWriteValue");
appbox::test::Probe appbox::test::ProbeRegQueryKeyName("RegQueryKeyName");
appbox::test::Probe appbox::test::ProbeRegQueryMultipleValues("RegQueryMultipleValues");
appbox::test::Probe appbox::test::ProbeRegReadValue("RegReadValue");
appbox::test::Probe appbox::test::ProbeRegReadValues("RegReadValues");
appbox::test::Probe appbox::test::ProbeRegRenameKey("RegRenameKey");
appbox::test::Probe appbox::test::ProbeRegSaveKey("RegSaveKey");
appbox::test::Probe appbox::test::ProbeRegShadowRead("RegShadowRead");
appbox::test::Probe appbox::test::ProbeRegTransacted("RegTransacted");
appbox::test::Probe appbox::test::ProbeRegTwoProcesses("RegTwoProcesses");
appbox::test::Probe appbox::test::ProbeRegWriteValue("RegWriteValue");
appbox::test::Probe appbox::test::ProbeRemoveDirectory("RemoveDirectory");
appbox::test::Probe appbox::test::ProbeReparsePoint("ReparsePoint");
appbox::test::Probe appbox::test::ProbeResolveName("ResolveName");
appbox::test::Probe appbox::test::ProbeSetInformationFile("SetInformationFile");
appbox::test::Probe appbox::test::ProbeSocketTraffic("SocketTraffic");
appbox::test::Probe appbox::test::ProbeStartupStarted("StartupStarted");
appbox::test::Probe appbox::test::ProbeWriteFile("WriteFile");
