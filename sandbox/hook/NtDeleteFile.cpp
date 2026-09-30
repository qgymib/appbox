#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Defines.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "filesystem/CreateDirectory.hpp"
#include "filesystem/DirName.hpp"
#include "filesystem/IsolationPolicy.hpp"
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
 * @brief Whether the delete of an entry is applied to the host filesystem.
 * @param[in] resolve_result Resolve result.
 * @return true when the entry of the host filesystem has to be removed.
 */
static bool DeletesInHost(const appbox::filesystem::ResolveResult& resolve_result)
{
    return appbox::filesystem::WritesToHost(resolve_result.isolation, resolve_result.bHostHolds,
                                            resolve_result.bSandboxHolds);
}

/**
 * @brief Remove the entry of the host filesystem which the isolation names.
 *
 * The delete is the modification of the isolation mode, so a `Merge` entry the
 * host filesystem holds is really removed from the host. An entry which is
 * already gone is not an error: a handle which was opened on the host entry
 * and marked for deletion removes it when it is closed, before the sandbox
 * records the delete.
 *
 * @param[in] resolve_result Resolve result.
 * @param[in] Attributes File attributes.
 * @return NTSTATUS
 */
static NTSTATUS DeleteHostEntry(const appbox::filesystem::ResolveResult& resolve_result, ULONG Attributes)
{
    const NTSTATUS st = NtDeleteFileWrap(resolve_result.hostPath, Attributes);
    if (!NT_SUCCESS(st) && st != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return st;
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Whether a whiteout marker has to hide the entry after its delete.
 *
 * The marker hides the layers which still hold the entry once the layers the
 * delete removed are gone. Without a delete of the host entry the upper copy is
 * the only layer which was removed, so the marker is needed while the entry
 * came from another layer or while the upper copy was not there at all; a
 * `Merge` delete removed the entry of the host filesystem as well, so only the
 * layers below the upper one can still hold it.
 *
 * @param[in] resolve_result Resolve result of the entry.
 * @param[in] host_removed Whether the entry of the host filesystem was removed.
 * @return true when a marker has to be written.
 */
static bool NeedsWhiteout(const appbox::filesystem::ResolveResult& resolve_result, bool host_removed)
{
    if (!host_removed)
    {
        return !resolve_result.bInUpper || resolve_result.hPath.size() > 1;
    }

    for (const auto& path : resolve_result.hPath)
    {
        /* The upper copy was removed above and the host entry was removed by
         * the caller, so only a lower layer can still hold the entry. */
        if (path.layer != 0 && path.layer != resolve_result.hostLayer)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Write the whiteout marker which hides the layers below the upper one.
 *
 * The marker is a sibling of the entry inside the upper layer, so the folders
 * above it have to exist first: the entry which was removed may be one which
 * only a lower layer or the host filesystem holds, in which case the upper
 * layer does not carry its folders yet.
 *
 * @param[in] resolve_result Resolve result of the entry.
 * @param[in] Attributes File attributes.
 * @return NTSTATUS
 */
static NTSTATUS CreateWhiteoutOfEntry(const appbox::filesystem::ResolveResult& resolve_result, ULONG Attributes)
{
    appbox::filesystem::CreateDirectories(appbox::filesystem::DirName(resolve_result.uPath),
                                          resolve_result.uPathBaseSize);
    return CreateWhiteout(resolve_result.uPath, Attributes);
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

    LOG_T("meta: {}", appbox::DumpJson(nlohmann::json(resolve_result)));

    /* The layer the isolation names is modified first, see DeleteHostEntry(). */
    const bool deletes_host = DeletesInHost(resolve_result);
    if (deletes_host)
    {
        LOG_T(L"delete: target=host");
        st = DeleteHostEntry(resolve_result, Attributes);
        if (!NT_SUCCESS(st))
        {
            return st;
        }
    }

    /* If file exists in upper filesystem, delete it. */
    if (resolve_result.bInUpper)
    {
        st = NtDeleteFileWrap(resolve_result.uPath, Attributes);
    }

    /* The marker hides the layers which still hold the entry. */
    if (NeedsWhiteout(resolve_result, deletes_host))
    {
        st = CreateWhiteoutOfEntry(resolve_result, Attributes);
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
 * @brief Remove the folder of the host filesystem which the isolation names.
 *
 * The folder is removed with everything below it, because the empty check of
 * DeleteAsDirectory() already ran over the folder of the host layer. A folder
 * which is already gone is not an error, see DeleteHostEntry().
 *
 * @param[in] resolve_result Resolve result.
 * @param[in] Attributes File attributes.
 * @return NTSTATUS
 */
static NTSTATUS DeleteHostDirectory(const appbox::filesystem::ResolveResult& resolve_result, ULONG Attributes)
{
    const NTSTATUS st = appbox::filesystem::RemoveAll(resolve_result.hostPath, Attributes);
    if (!NT_SUCCESS(st) && st != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return st;
    }
    return STATUS_SUCCESS;
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

    /* The layer the isolation names is modified first, see DeleteHostEntry(). */
    const bool deletes_host = DeletesInHost(resolve_result);
    if (deletes_host)
    {
        LOG_T(L"delete: target=host");
        st = DeleteHostDirectory(resolve_result, Attributes);
        if (!NT_SUCCESS(st))
        {
            return st;
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
    if (NeedsWhiteout(resolve_result, deletes_host))
    {
        st = CreateWhiteoutOfEntry(resolve_result, Attributes);
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
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));

    return DeleteViewPath(*resolve_result, Attributes);
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
