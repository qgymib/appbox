#include "utils/WinAPI.h" /* Must be first include file */
#include <list>
#include <mutex>
#include <set>
#include <string>
#include "utils/DirectoryInformationWalker.hpp"
#include "utils/HandleInfo.hpp"
#include "utils/Log.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryDirectoryFile.hpp"
#include "hook/NtQueryDirectoryFileEx.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "filesystem/MarkerName.hpp"
#include "Resolve.hpp"
#include "ViewPathOfHandle.hpp"
#include "DirectoryMerge.hpp"

/**
 * @brief State of the enumeration of one directory handle.
 *
 * The state is shared by the two entry points and by every information class
 * which enumerates a directory of the view, so a caller may mix them on the
 * same handle.
 */
struct FullDirectoryInformationMeta : appbox::HandleInfo::Meta
{
    typedef std::shared_ptr<FullDirectoryInformationMeta> Ptr;

    FullDirectoryInformationMeta(appbox::HandleInfo::Ptr info, PUNICODE_STRING FileName);
    ~FullDirectoryInformationMeta() override;

    std::mutex              mutex;         /* Context mutex */
    ULONG                   ObjAttributes; /* OBJ_CASE_INSENSITIVE */
    std::wstring            BasePath;      /* Base directory path. */
    std::wstring            FileName;      /* Search pattern */
    std::list<std::wstring> PendingDir;    /* Pending query directory */
    HANDLE                  DirHandle;     /* Current directory handle */
    std::set<std::wstring>  FileNameSeen;  /* File names that already seen. */
};

/**
 * @brief Key of the state of an enumeration inside `HandleInfo`.
 *
 * The key is the address of a translation unit local object, so every entry
 * point shares one state per handle.
 */
static const char     s_meta_key = 0;
static const uint64_t FullDirectoryInformationMetaKey = reinterpret_cast<uint64_t>(&s_meta_key);

/**
 * @brief What the view does with a directory handle an enumeration names.
 */
struct DirectoryHandle
{
    /** What the view does with the call. */
    enum class Action
    {
        Merge,   /* The record of the handle answers the enumeration. */
        Forward, /* The handle denotes no directory of the view. */
        Refuse,  /* The view refuses the call with `status`. */
    };

    Action                  action = Action::Forward; /* Action of the view. */
    appbox::HandleInfo::Ptr info;                     /* Record of the handle. */
    NTSTATUS                status = STATUS_SUCCESS;  /* Status of a refusal. */
};

/**
 * @brief The record of the directory handle an enumeration names.
 *
 * The record of an open the sandbox performed is authoritative. A handle the
 * sandbox did not open, which is the handle a process inherited or duplicated,
 * is adopted here: the path of the view the handle denotes is looked up in the
 * file system (see `ViewPathOfHandle`) and resolved, and the record which is
 * built from the result is what the merge reads the layers of the directory
 * from. The record is dropped while the handle is closed, like the record of an
 * open, so the state of the enumeration lives exactly as long as the handle.
 *
 * @param[in] handle Handle to look up.
 * @return What the view does with the call, together with the record it
 *         answers with.
 */
static DirectoryHandle ResolveDirectoryHandle(HANDLE handle)
{
    DirectoryHandle result;

    auto info = appbox::HandleInfo::Find(handle);
    if (info.get() != nullptr && !info->viewPath.empty())
    {
        result.action = DirectoryHandle::Action::Merge;
        result.info = info;
        return result;
    }

    std::wstring viewPath;
    switch (appbox::filesystem::ViewPathOfHandle(handle, viewPath))
    {
    case appbox::filesystem::HandlePathStatus::Foreign:
        /* The object belongs to another isolation domain, which answers it. */
        return result;

    case appbox::filesystem::HandlePathStatus::Unnamed:
        /*
         * A local object the view cannot name: the answer of the layer the
         * handle was opened with would show the entries the view hides and the
         * markers of the view themselves, so the call is refused instead of
         * being forwarded.
         */
        result.action = DirectoryHandle::Action::Refuse;
        result.status = STATUS_NOT_SUPPORTED;
        return result;

    case appbox::filesystem::HandlePathStatus::View:
        break;
    }

    /*
     * The merge walks every layer which holds the directory, and the layers
     * decide whether the view holds the entry at all, so the resolver is asked
     * for all of them.
     */
    appbox::filesystem::ResolveOption option;
    option.NameAttributes = OBJ_CASE_INSENSITIVE;
    option.bStopOnFirstFound = false;

    auto resolve = appbox::filesystem::Resolve(viewPath, option);
    LOG_T(L"adopt: {}", viewPath);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve)));

    if (resolve->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        /*
         * The entry is not part of the view: a whiteout, an opaque marker or
         * the isolation of the view hides it, and the answer a call which names
         * the path receives is that the entry does not exist.
         */
        result.action = DirectoryHandle::Action::Refuse;
        result.status = STATUS_OBJECT_NAME_NOT_FOUND;
        return result;
    }

    if (resolve->hPath.empty() || (resolve->hPath[0].fInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        /*
         * The handle denotes no directory, so the view has nothing to merge and
         * the file system reports its own failure for the enumeration.
         */
        return result;
    }

    auto adopted = appbox::HandleInfo::Create(handle, [&viewPath, &resolve](appbox::HandleInfo::Ptr record) {
        record->viewPath = viewPath;
        record->resolve = resolve;
        record->ObjAttributes = OBJ_CASE_INSENSITIVE;
        record->bDeleteOnClose = false;
        record->bAdopted = true;
    });

    if (adopted.get() == nullptr)
    {
        /*
         * The record could not be created: another thread adopted the handle
         * first, or the module is not initialized. The record of that thread
         * describes the same directory, so it is used here.
         */
        adopted = appbox::HandleInfo::Find(handle);
        if (adopted.get() == nullptr || adopted->viewPath.empty())
        {
            return result;
        }
    }

    result.action = DirectoryHandle::Action::Merge;
    result.info = adopted;
    return result;
}

static std::wstring ToLower(const std::wstring& str)
{
    int len = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, str.c_str(), (int)str.size(), nullptr, 0,
                            nullptr, nullptr, 0);

    std::wstring result(len, 0);
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, str.c_str(), (int)str.size(), result.data(), len, nullptr,
                  nullptr, 0);
    return result;
}

/**
 * @brief Drop the entries of one buffer which the view does not show.
 *
 * @param[in] meta State of the enumeration.
 * @param[in] IoStatusBlock Status block of the call.
 * @param[in,out] FileInformation Buffer of the caller.
 * @param[in] layout Layout of the information class of the buffer.
 */
static void FixNameInfo(FullDirectoryInformationMeta::Ptr meta, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                        const appbox::DirectoryInformationLayout& layout)
{
    if (IoStatusBlock->Information == 0)
    {
        return;
    }

    IoStatusBlock->Information = appbox::DirectoryInformationWalker::Walk(
        FileInformation, IoStatusBlock->Information, layout, [&meta](void* entry, const std::wstring& name) {
            (void)entry;

            /*
             * The names of the markers are reserved, so they are never part of
             * the view, see `MarkerName.hpp`.
             */
            if (appbox::filesystem::IsReservedMarkerName(name))
            {
                return true;
            }

            /*
             * An entry which the view does not hold is dropped: this is what
             * makes a whiteout, an opaque marker and the isolation of the view
             * effective in a listing.
             */
            {
                auto                              fullPath = meta->BasePath + L"\\" + name;
                appbox::filesystem::ResolveOption option;
                option.NameAttributes = meta->ObjAttributes;
                auto res = appbox::filesystem::Resolve(fullPath, option);
                if (res->status != appbox::filesystem::ResolveResult::Status::Exists)
                {
                    return true;
                }
            }

            /* A name which an earlier layer reported is dropped as well. */
            std::wstring key = name;
            if (meta->ObjAttributes & OBJ_CASE_INSENSITIVE)
            {
                key = ToLower(key);
            }

            /* The caller holds meta->mutex, see QueryDirectoryInformation(). */
            auto it = meta->FileNameSeen.find(key);
            if (it == meta->FileNameSeen.end())
            {
                meta->FileNameSeen.insert(key);
                return false;
            }

            return true;
        });
}

FullDirectoryInformationMeta::FullDirectoryInformationMeta(appbox::HandleInfo::Ptr info, PUNICODE_STRING FileName)
{
    this->ObjAttributes = info->resolve->NameAttributes;
    this->BasePath = info->viewPath;
    this->DirHandle = nullptr;

    /*
     * The search pattern is optional: a caller may enumerate a directory
     * without one, which reports every entry.
     */
    if (FileName != nullptr && FileName->Buffer != nullptr)
    {
        this->FileName.assign(FileName->Buffer, FileName->Length / sizeof(WCHAR));
    }

    for (const auto& p : info->resolve->hPath)
    {
        if (!(p.fInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            break;
        }
        this->PendingDir.push_back(p.fPath);
    }

    while (!this->BasePath.empty() && this->BasePath.back() == L'\\')
    {
        this->BasePath.pop_back();
    }
}

FullDirectoryInformationMeta::~FullDirectoryInformationMeta()
{
    if (DirHandle != nullptr)
    {
        sys_NtClose(DirHandle);
        DirHandle = nullptr;
    }
}

/**
 * @brief Open the next layer of the directory which is being enumerated.
 *
 * @param[in] meta The state of the enumeration.
 * @return Status code, `STATUS_NO_MORE_FILES` when every layer was visited.
 */
static NTSTATUS NtQueryDirectoryFilePopOne(FullDirectoryInformationMeta::Ptr meta)
{
    if (meta->DirHandle != nullptr)
    {
        sys_NtClose(meta->DirHandle);
        meta->DirHandle = nullptr;
    }

    if (meta->PendingDir.empty())
    {
        return STATUS_NO_MORE_FILES;
    }

    auto dir = meta->PendingDir.front();
    meta->PendingDir.pop_front();
    LOG_T(L"pop to dir={}", dir);

    UNICODE_STRING usPath;
    sys_RtlInitUnicodeString(&usPath, dir.c_str());

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &usPath, meta->ObjAttributes, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;

    return sys_NtOpenFile(&meta->DirHandle, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 16417);
}

/**
 * @brief Query one layer of the directory which is being enumerated.
 *
 * The entry point of the caller is used, so the merge works on systems which
 * provide the plain entry point only.
 *
 * @param[in] extended true when the caller used the extended entry point.
 * @param[in] dir Handle of the layer directory.
 * @param[in] IoStatusBlock Status block of the call.
 * @param[in] FileInformation Caller buffer.
 * @param[in] Length Size of the buffer.
 * @param[in] QueryFlags `kQueryRestartScan` and `kQueryReturnSingleEntry`.
 * @param[in] FileName Search pattern of the call.
 * @param[in] FileInformationClass Information class of the call.
 * @return Status code.
 */
static NTSTATUS QueryLayerDirectory(bool extended, HANDLE dir, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                    ULONG Length, ULONG QueryFlags, PUNICODE_STRING FileName,
                                    FILE_INFORMATION_CLASS FileInformationClass)
{
    if (extended)
    {
        return sys_NtQueryDirectoryFileEx(dir, nullptr, nullptr, nullptr, IoStatusBlock, FileInformation, Length,
                                          FileInformationClass, QueryFlags, FileName);
    }

    return sys_NtQueryDirectoryFile(dir, nullptr, nullptr, nullptr, IoStatusBlock, FileInformation, Length,
                                    FileInformationClass,
                                    (QueryFlags & appbox::filesystem::kQueryReturnSingleEntry) != 0, FileName,
                                    (QueryFlags & appbox::filesystem::kQueryRestartScan) != 0);
}

NTSTATUS appbox::filesystem::QueryDirectoryInformation(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                       PVOID FileInformation, ULONG Length, ULONG QueryFlags,
                                                       PUNICODE_STRING        FileName,
                                                       FILE_INFORMATION_CLASS FileInformationClass, bool extended,
                                                       bool& handled)
{
    handled = false;

    /*
     * The handle decides whether the view answers the call at all: the record
     * of an open the sandbox performed, or the record the sandbox adopts here,
     * names the layers of the enumeration, while a handle which denotes no
     * directory of the view is left to the file system.
     */
    auto directory = ResolveDirectoryHandle(FileHandle);
    if (directory.action == DirectoryHandle::Action::Forward)
    {
        return STATUS_INVALID_HANDLE;
    }

    handled = true;
    if (directory.action == DirectoryHandle::Action::Refuse)
    {
        return directory.status;
    }

    auto info = directory.info;
    LOG_T("info={}", (void*)info.get());

    appbox::DirectoryInformationLayout layout;
    if (!appbox::DirectoryInformationLayoutOf(FileInformationClass, layout))
    {
        /*
         * The view never answers with the content of a single layer: a class
         * the merge cannot read is refused, because the answer of the layer the
         * handle was opened with would list the entries a whiteout, an opaque
         * marker or the isolation hides, together with the markers of the view
         * themselves.
         */
        return STATUS_NOT_SUPPORTED;
    }

    if ((QueryFlags & kQueryRestartScan) != 0)
    {
        info->MetaDrop(FullDirectoryInformationMetaKey);
    }

    auto meta = std::dynamic_pointer_cast<FullDirectoryInformationMeta>(
        info->MetaFindOr(FullDirectoryInformationMetaKey, [FileName, info]() -> appbox::HandleInfo::Meta::Ptr {
            return std::make_shared<FullDirectoryInformationMeta>(info, FileName);
        }));

    if (meta == nullptr)
    {
        /* Should not happen, the meta is created with this type above. */
        return STATUS_INVALID_HANDLE;
    }

    /*
     * The meta data is shared by every thread which enumerates the same
     * directory handle, so every access to it is serialized here. FixNameInfo()
     * expects the lock to be held by its caller.
     */
    std::lock_guard<std::mutex> guard(meta->mutex);

    /* Update filename if necessary */
    if (FileName != nullptr && FileName->Buffer != nullptr)
    {
        meta->FileName.assign(FileName->Buffer, FileName->Length / sizeof(WCHAR));
        LOG_T(L"FileName={}", meta->FileName);
    }

    /*
     * The restart flag applies to the first query of the call only: the merge
     * queries the layer again when it dropped an entry, and a restart would
     * report that entry forever.
     */
    ULONG layer_flags = QueryFlags;

    for (;;)
    {
        if (meta->DirHandle == nullptr)
        {
            const NTSTATUS st = NtQueryDirectoryFilePopOne(meta);
            if (!NT_SUCCESS(st))
            {
                return st;
            }
        }

        UNICODE_STRING usName;
        sys_RtlInitUnicodeString(&usName, meta->FileName.c_str());

        const NTSTATUS st = QueryLayerDirectory(extended, meta->DirHandle, IoStatusBlock, FileInformation, Length,
                                                layer_flags, &usName, FileInformationClass);
        layer_flags &= ~kQueryRestartScan;
        if (NT_SUCCESS(st))
        {
            auto oldInformation = IoStatusBlock->Information;
            FixNameInfo(meta, IoStatusBlock, FileInformation, layout);
            if (IoStatusBlock->Information == 0 && oldInformation != 0)
            {
                continue;
            }
            return st;
        }

        if (st != STATUS_NO_MORE_FILES)
        {
            return st;
        }

        sys_NtClose(meta->DirHandle);
        meta->DirHandle = nullptr;
    }

    return STATUS_NO_MORE_FILES;
}
