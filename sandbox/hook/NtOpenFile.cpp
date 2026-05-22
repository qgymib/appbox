#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtCreateFile.hpp"
#include "NtOpenFile.hpp"
#include "__init__.hpp"
#include <detours.h>

static nlohmann::json NtOpenFileLogParam(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                         POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
                                         ULONG ShareAccess, ULONG OpenOptions)
{
    nlohmann::json json;
    json["FileHandle"] = appbox::PointerToString(FileHandle);
    json["DesiredAccess"] = DesiredAccess;
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes, OpenOptions);
    json["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    json["ShareAccess"] = ShareAccess;
    json["OpenOptions"] = OpenOptions;
    return json;
}

T_NtOpenFile           sys_NtOpenFile = nullptr;
static appbox::LoggerF logger("NtOpenFile", NtOpenFileLogParam);

static NTSTATUS Hook_NtOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    {
        appbox::NtCreateFileLock lock;
        logger.Log(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
    }

    // TODO
    return sys_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
}

void appbox::InjectNtOpenFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtOpenFile");
    sys_NtOpenFile = reinterpret_cast<T_NtOpenFile>(addr);

    DetourAttach(&sys_NtOpenFile, Hook_NtOpenFile);
}
