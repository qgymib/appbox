#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtQueryObject.hpp"
#include "__init__.hpp"
#include <detours.h>

T_NtQueryObject sys_NtQueryObject = nullptr;

static nlohmann::json NtQueryObjectLogParam(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                            PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    nlohmann::json json;
    json["Handle"] = appbox::PointerToString(Handle);
    json["ObjectInformationClass"] = ObjectInformationClass;
    json["ObjectInformation"] = appbox::PointerToString(ObjectInformation);
    json["ObjectInformationLength"] = ObjectInformationLength;
    json["ReturnLength"] = appbox::PointerToString(ReturnLength);
    return json;
}

static appbox::LoggerF logger("NtQueryObject", NtQueryObjectLogParam);

static NTSTATUS Hook_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass,
                                   PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    if (appbox::ThreadLocal::Get().disable_NtQueryObject_hook)
    {
        return sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength,
                                 ReturnLength);
    }

    {
        appbox::NtCreateFileLock lock;
        logger.Log(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);
    }

    // TODO
    return sys_NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);
}

void appbox::InjectNtQueryObject()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtQueryObject");
    sys_NtQueryObject = reinterpret_cast<T_NtQueryObject>(addr);

    DetourAttach(&sys_NtQueryObject, Hook_NtQueryObject);
}
