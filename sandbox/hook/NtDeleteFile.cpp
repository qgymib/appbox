#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtDeleteFile.hpp"
#include "__init__.hpp"
#include "WString.hpp"
#include <detours.h>

T_NtDeleteFile         sys_NtDeleteFile = nullptr;
static appbox::LoggerF logger("NtDeleteFile",
                              [](POBJECT_ATTRIBUTES ObjectAttributes) { return appbox::ToJson(ObjectAttributes); });

static NTSTATUS Hook_NtDeleteFile(POBJECT_ATTRIBUTES ObjectAttributes)
{
    logger.Log(ObjectAttributes);

    // TODO
    return sys_NtDeleteFile(ObjectAttributes);
}

void appbox::InjectNtDeleteFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtDeleteFile");
    sys_NtDeleteFile = reinterpret_cast<T_NtDeleteFile>(addr);

    DetourAttach(&sys_NtDeleteFile, Hook_NtDeleteFile);
}
