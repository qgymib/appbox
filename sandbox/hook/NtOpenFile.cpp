#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "utils/CopyFileNt.hpp"
#include "utils/HandleInfo.hpp"
#include "utils/ConvertToFullNtPath.hpp"
#include "filesystem/Resolve.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "NtOpenFile.hpp"

T_NtOpenFile sys_NtOpenFile = nullptr;

static nlohmann::json NtOpenFileLogParam(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                         POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
                                         ULONG ShareAccess, ULONG OpenOptions)
{
    nlohmann::json json;
    json["FileHandle"] = appbox::PointerToString(FileHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes, OpenOptions);
    json["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    json["ShareAccess"] = ShareAccess;
    json["OpenOptions"] = OpenOptions;
    return json;
}

static appbox::LoggerF logger("NtOpenFile", NtOpenFileLogParam);

static NTSTATUS NtOpenFileWrap(const std::wstring& path, ULONG Attributes, PHANDLE FileHandle,
                               ACCESS_MASK DesiredAccess, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess,
                               ULONG OpenOptions)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);

    return sys_NtOpenFile(FileHandle, DesiredAccess, &oa, IoStatusBlock, ShareAccess, OpenOptions);
}

static bool NtOpenFileGetDosNtPath(POBJECT_ATTRIBUTES ObjectAttributes, std::wstring& path)
{
    std::wstring nativate_fs_nt_path;
    if (appbox::ConvertToFullNtPath(ObjectAttributes, 0, nativate_fs_nt_path) != 0)
    {
        return false;
    }

    std::wstring nativate_fs_path;
    if (!appbox::MappingAsDosNtPath(nativate_fs_nt_path, nativate_fs_path))
    {
        return false;
    }

    path = nativate_fs_path;
    return true;
}

static NTSTATUS Hook_NtOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    logger.Log(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);

    std::wstring nativate_fs_path;
    if (!NtOpenFileGetDosNtPath(ObjectAttributes, nativate_fs_path))
    {
        return sys_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
    }

    /* Resolve path in sandbox. */
    appbox::filesystem::ResolveOption resolve_option;
    resolve_option.NameAttributes = ObjectAttributes->Attributes;
    resolve_option.bStopOnFirstFound = false;

    auto resolve_result = appbox::filesystem::Resolve(nativate_fs_path, resolve_option);
    LOG_T("resolve: {}", nlohmann::json(*resolve_result).dump());

    if (!resolve_result->bParentExist)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }
    if (resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    const bool want_edit = (DesiredAccess & (DELETE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
                                             FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL));
    if (want_edit && !resolve_result->bInUpper)
    {
        appbox::CopyFileNt(resolve_result->hPath[0].fPath, resolve_result->uPath);
        resolve_result->bInUpper = true;

        appbox::filesystem::ResolveResult::Path p;
        p.fPath = resolve_result->uPath;
        p.fInfo = resolve_result->hPath[0].fInfo;
        resolve_result->hPath.insert(resolve_result->hPath.begin(), p);
    }

    std::wstring open_path = resolve_result->bInUpper ? resolve_result->uPath : resolve_result->hPath[0].fPath;
    auto         st = NtOpenFileWrap(open_path, ObjectAttributes->Attributes, FileHandle, DesiredAccess, IoStatusBlock,
                                     ShareAccess, OpenOptions);
    if (NT_SUCCESS(st))
    {
        appbox::HandleInfo::Create(
            *FileHandle, [&nativate_fs_path, ObjectAttributes, &resolve_result](appbox::HandleInfo::Ptr info) {
                info->viewPath = nativate_fs_path;
                info->resolve = resolve_result;
                info->ObjAttributes = ObjectAttributes->Attributes;
            });
    }
    return st;
}

static void LoadNtOpenFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtOpenFile");
    sys_NtOpenFile = reinterpret_cast<T_NtOpenFile>(addr);
}

appbox::HookRecord appbox::HookNtOpenFile = {
    "NtOpenFile",
    LoadNtOpenFile,
    (void**)&sys_NtOpenFile,
    Hook_NtOpenFile,
};
