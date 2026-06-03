#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Defines.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "filesystem/Resolve.hpp"
#include "filesystem/RemoveAll.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "NtDeleteFile.hpp"
#include "WString.hpp"
#include "Config.hpp"

struct FolderTraversalResult
{
    struct Item
    {
        std::wstring name;
        bool         bIsDirectory;
    };
    std::vector<Item> items;
};

T_NtDeleteFile         sys_NtDeleteFile = nullptr;
static appbox::LoggerF logger("NtDeleteFile",
                              [](POBJECT_ATTRIBUTES ObjectAttributes) { return appbox::ToJson(ObjectAttributes); });

/**
 * @brief Delete single item.
 * @param[in] path Path to delete.
 * @param[in] Attributes Attributes.
 * @return NTSTATUS.
 */
static NTSTATUS NtDeleteFileWrap(const std::wstring& path, ULONG Attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);
    return sys_NtDeleteFile(&oa);
}

static NTSTATUS CreateWhiteout(const std::wstring& path, ULONG Attributes)
{
    auto whiteout_path = path + APPBOX_SANDBOX_WHITEOUT_SUFFIX_W;

    IO_STATUS_BLOCK   iosb;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    usPath;
    sys_RtlInitUnicodeString(&usPath, whiteout_path.c_str());
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);

    HANDLE hFile = nullptr;
    auto   nt = sys_NtCreateFile(&hFile, DELETE | FILE_WRITE_DATA, &oa, &iosb, nullptr, FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN_IF,
                                 FILE_NON_DIRECTORY_FILE, nullptr, 0);
    if (NT_SUCCESS(nt))
    {
        sys_NtClose(hFile);
    }
    return nt;
}

/**
 * @brief Delete file
 * @param[in] resolve_result Resolve result.
 * @param[in] Attributes File attributes.
 * @return NTSTATUS
 */
static NTSTATUS DeleteAsFile(const appbox::filesystem::ResolveResult& resolve_result, ULONG Attributes)
{
    NTSTATUS st = STATUS_SUCCESS;

    LOG_T("meta: {}", nlohmann::json(resolve_result).dump());

    /* If file exists in upper filesystem, delete it. */
    if (resolve_result.bInUpper)
    {
        st = NtDeleteFileWrap(resolve_result.uPath, Attributes);
    }
    /* If file also exists in lower / host filesystem, add whiteout file. */
    if (!resolve_result.bInUpper || resolve_result.hPath.size() > 1)
    {
        st = CreateWhiteout(resolve_result.uPath, Attributes);
    }
    return st;
}

static FolderTraversalResult FolderTraversal(const std::wstring& path, ULONG Attributes)
{
    FolderTraversalResult result;

    UNICODE_STRING usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);

    HANDLE          hDir = nullptr;
    IO_STATUS_BLOCK iosb;

    auto st = sys_NtOpenFile(&hDir, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(st))
    {
        return result;
    }

    std::vector<BYTE> buff(64 * 1024);
    for (;;)
    {
        st = sys_NtQueryDirectoryFile(hDir, nullptr, nullptr, nullptr, &iosb, buff.data(),
                                      static_cast<ULONG>(buff.size()), FileDirectoryInformation, false, nullptr, false);
        if (!NT_SUCCESS(st))
        {
            break;
        }

        auto info = reinterpret_cast<FILE_DIRECTORY_INFORMATION*>(buff.data());
        for (;;)
        {
            FolderTraversalResult::Item item;
            item.name = std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR));
            if (item.name != L"." && item.name != L"..")
            {
                item.bIsDirectory = !!(info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
                result.items.push_back(item);
            }

            if (info->NextEntryOffset == 0)
            {
                break;
            }

            info = reinterpret_cast<FILE_DIRECTORY_INFORMATION*>(reinterpret_cast<BYTE*>(info) + info->NextEntryOffset);
        }
    }

    sys_NtClose(hDir);
    return result;
}

static bool EndsWith(const std::wstring& str, const std::wstring& suffix)
{
    auto str_sz = str.size();
    auto suffix_sz = suffix.size();
    if (suffix_sz > str_sz)
    {
        return false;
    }
    return str.compare(str_sz - suffix_sz, suffix_sz, suffix) == 0;
}

/**
 * @brief Delete directory
 * @param[in] resolve_result Resolve result.
 * @param[in] Attributes File attributes.
 * @return NTSTATUS
 */
static NTSTATUS DeleteAsDirectory(const appbox::filesystem::ResolveResult& resolve_result, ULONG Attributes)
{
    NTSTATUS st = 0;

    /* In fs view, check if any file (except whiteout and opaque file) exist. */
    for (const auto& fs : resolve_result.hPath)
    {
        auto traversal_result = FolderTraversal(fs.fPath, Attributes);
        for (const auto& item : traversal_result.items)
        {
            if (item.name != APPBOX_SANDBOX_OPAQUE_NAME_W && !EndsWith(item.name, APPBOX_SANDBOX_WHITEOUT_SUFFIX_W))
            {
                return STATUS_DIRECTORY_NOT_EMPTY;
            }
        }
    }

    if (resolve_result.bInUpper)
    {
        st = appbox::filesystem::RemoveAll(resolve_result.uPath, Attributes);
        if (!NT_SUCCESS(st))
        {
            return st;
        }
    }
    if (resolve_result.hPath.size() > 1 || !resolve_result.bInUpper)
    {
        st = CreateWhiteout(resolve_result.uPath, Attributes);
        if (!NT_SUCCESS(st))
        {
            return st;
        }
    }

    return 0;
}

static NTSTATUS Hook_NtDeleteFile(POBJECT_ATTRIBUTES ObjectAttributes)
{
    logger.Log(ObjectAttributes);

    std::wstring fs_path;
    if (!appbox::MappingAsDosNtPath(ObjectAttributes->ObjectName->Buffer, fs_path))
    {
        return sys_NtDeleteFile(ObjectAttributes);
    }

    return appbox::DeleteViewPath(fs_path, ObjectAttributes->Attributes);
}

NTSTATUS appbox::DeleteViewPath(const std::wstring& path, ULONG Attributes)
{
    appbox::filesystem::ResolveOption resolve_option;
    resolve_option.bStopOnFirstFound = false;

    auto resolve_result = appbox::filesystem::Resolve(path, resolve_option);
    LOG_T("resolve: {}", nlohmann::json(resolve_result).dump());

    return DeleteViewPath(resolve_result, Attributes);
}

NTSTATUS appbox::DeleteViewPath(const appbox::filesystem::ResolveResult& resolve, ULONG Attributes)
{
    if (!resolve.bParentExist)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }
    if (resolve.status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (!(resolve.hPath[0].fInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        LOG_T("delete as file");
        return DeleteAsFile(resolve, Attributes);
    }
    LOG_T("delete as dir");
    return DeleteAsDirectory(resolve, Attributes);
}

static void LoadNtDeleteFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtDeleteFile");
    sys_NtDeleteFile = reinterpret_cast<T_NtDeleteFile>(addr);
}

appbox::HookRecord appbox::HookNtDeleteFile = {
    "NtDeleteFile",
    LoadNtDeleteFile,
    (void**)&sys_NtDeleteFile,
    Hook_NtDeleteFile,
};
