#include "WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include "ConsoleWindow.hpp"
#include "CreateDirectoryW.hpp"
#include "CreateFileW.hpp"
#include "DeleteFileW.hpp"
#include "EnvironmentRead.hpp"
#include "EnvironmentWrite.hpp"
#include "Exit.hpp"
#include "FindStreams.hpp"
#include "Fonts.hpp"
#include "Job.hpp"
#include "LaunchProcess.hpp"
#include "ListDir.hpp"
#include "ListDirNt.hpp"
#include "LoadedModules.hpp"
#include "QueryAttributes.hpp"
#include "QueryFullAttributes.hpp"
#include "QueryInformationByName.hpp"
#include "QueryInformationFile.hpp"
#include "ReadFileFull.hpp"
#include "RegDeleteKey.hpp"
#include "RegDeleteValue.hpp"
#include "RegEnumKey.hpp"
#include "RegEnumValue.hpp"
#include "RegKeyApi.hpp"
#include "RegOpenWriteValue.hpp"
#include "RegQueryKeyName.hpp"
#include "RegQueryMultipleValues.hpp"
#include "RegReadValue.hpp"
#include "RegReadValues.hpp"
#include "RegRenameKey.hpp"
#include "RegSaveKey.hpp"
#include "RegShadowRead.hpp"
#include "RegTransacted.hpp"
#include "RegTwoProcesses.hpp"
#include "RegWriteValue.hpp"
#include "RemoveDirectory.hpp"
#include "ReparsePoint.hpp"
#include "ResolveName.hpp"
#include "SetInformationFile.hpp"
#include "SocketTraffic.hpp"
#include "StartupStarted.hpp"
#include "WriteFile.hpp"

/*
 * The translation unit of the method table of the probe executable.
 *
 * The table is built here and not in `test/probe/__init__.cpp`, because the
 * headers of the probes bring the declarations of the system API with them,
 * while the runtime brings the socket headers of asio: the two cannot be
 * included in one translation unit. The runtime asks for the table with
 * `RegisterProbes()` and knows no probe of its own.
 */
#define APPBOX_PROBE_REGISTER(NAME)                                                                                    \
    appbox::test::RegisterProbe<appbox::test::Protocol##NAME>(&appbox::test::Probe##NAME##_Entry);

void appbox::test::RegisterProbes()
{
    APPBOX_PROBE_METHODS(APPBOX_PROBE_REGISTER)
}

#undef APPBOX_PROBE_REGISTER
