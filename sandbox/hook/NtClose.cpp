#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtClose.hpp"
#include "__init__.hpp"
#include <detours.h>

T_NtClose sys_NtClose = nullptr;

static nlohmann::json NtCloseLogParam(HANDLE Handle)
{
    nlohmann::json param;
    param["Handle"] = appbox::PointerToString(Handle);
    return param;
}
static appbox::LoggerF logger("NtClose", NtCloseLogParam);

static NTSTATUS Hook_NtClose(HANDLE Handle)
{
    logger.Log(Handle);
    return sys_NtClose(Handle);
}

void appbox::InjectNtClose()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryObject");
    sys_NtClose = reinterpret_cast<T_NtClose>(addr);

    DetourAttach(&sys_NtClose, Hook_NtClose);
}
