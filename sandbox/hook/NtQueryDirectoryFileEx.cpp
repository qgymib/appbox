#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/HandleInfo.hpp"
#include "utils/FileFullDirInformationWalker.hpp"
#include "utils/Defines.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "NtQueryDirectoryFileEx.hpp"
#include <list>
#include <mutex>
#include <set>

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

T_NtQueryDirectoryFileEx sys_NtQueryDirectoryFileEx = nullptr;
static const uint64_t    FullDirectoryInformationMetaKey = reinterpret_cast<uint64_t>(&sys_NtQueryDirectoryFileEx);

static std::wstring ToLower(const std::wstring& str)
{
    int len = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, str.c_str(), (int)str.size(), nullptr, 0,
                            nullptr, nullptr, 0);

    std::wstring result(len, 0);
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, str.c_str(), (int)str.size(), result.data(), len, nullptr,
                  nullptr, 0);
    return result;
}

static bool EndWith(const std::wstring& str, const std::wstring& shuffix)
{
    if (shuffix.size() > str.size())
    {
        return false;
    }
    return str.compare(str.size() - shuffix.size(), shuffix.size(), shuffix) == 0;
}

static void FixNameInfo(FullDirectoryInformationMeta::Ptr meta, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation)
{
    if (IoStatusBlock->Information == 0)
    {
        return;
    }

    IoStatusBlock->Information = appbox::FileFullDirInformationWalker::Walk(
        FileInformation, IoStatusBlock->Information, [&meta](PFILE_FULL_DIR_INFORMATION info) {
            std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
            if (EndWith(name, APPBOX_SANDBOX_WHITEOUT_SUFFIX_W) || name == APPBOX_SANDBOX_OPAQUE_NAME_W)
            {
                return true;
            }

            /* Check if file exists in mapped view. */
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

            if (meta->ObjAttributes & OBJ_CASE_INSENSITIVE)
            {
                name = ToLower(name);
            }

            std::lock_guard<std::mutex> guard(meta->mutex);
            auto                        it = meta->FileNameSeen.find(name);
            if (it == meta->FileNameSeen.end())
            {
                meta->FileNameSeen.insert(name);
                return false;
            }

            return true;
        });
}

FullDirectoryInformationMeta::FullDirectoryInformationMeta(appbox::HandleInfo::Ptr info, PUNICODE_STRING FileName)
{
    this->ObjAttributes = info->resolve->NameAttributes;
    this->BasePath = info->viewPath;
    this->FileName.assign(FileName->Buffer, FileName->Length / sizeof(WCHAR));
    this->DirHandle = nullptr;

    for (const auto& p : info->resolve->hPath)
    {
        if (!(p.fInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            break;
        }
        this->PendingDir.push_back(p.fPath);
    }

    while (this->BasePath.back() == L'\\')
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

static nlohmann::json NtQueryDirectoryFileExLogParam(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                                     PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
                                                     PVOID FileInformation, ULONG Length,
                                                     FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags,
                                                     PUNICODE_STRING FileName)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["Event"] = appbox::PointerToString(Event);
    param["ApcRoutine"] = appbox::PointerToString(ApcRoutine);
    param["ApcContext"] = appbox::PointerToString(ApcContext);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = FileInformationClass;
    param["QueryFlags"] = QueryFlags;
    param["FileName"] = appbox::ToJson(FileName);
    return param;
}
static appbox::LoggerF logger("NtQueryDirectoryFileEx", NtQueryDirectoryFileExLogParam);

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

static NTSTATUS NtQueryDirectoryFileFullDirectoryInformation(HANDLE FileHandle, HANDLE Event,
                                                             PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                                                             PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                                             ULONG Length, ULONG QueryFlags, PUNICODE_STRING FileName)
{
    NTSTATUS st;
    auto     info = appbox::HandleInfo::Find(FileHandle);
    LOG_T("info={}", (void*)info.get());
    if (info.get() == nullptr)
    { /* Fallback to original */
        return sys_NtQueryDirectoryFileEx(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation,
                                          Length, FileFullDirectoryInformation, QueryFlags, FileName);
    }

    if (QueryFlags & SL_RESTART_SCAN)
    {
        info->MetaDrop(FullDirectoryInformationMetaKey);
    }

    auto meta = std::dynamic_pointer_cast<FullDirectoryInformationMeta>(
        info->MetaFindOr(FullDirectoryInformationMetaKey, [FileName, info]() -> appbox::HandleInfo::Meta::Ptr {
            return std::make_shared<FullDirectoryInformationMeta>(info, FileName);
        }));

    /* Update filename if necessary */
    if (FileName != nullptr)
    {
        meta->FileName.assign(FileName->Buffer, FileName->Length / sizeof(WCHAR));
        LOG_T(L"FileName={}", meta->FileName);
    }

    for (;;)
    {
        if (meta->DirHandle == nullptr)
        {
            st = NtQueryDirectoryFilePopOne(meta);
            if (!NT_SUCCESS(st))
            {
                return st;
            }
        }

        UNICODE_STRING usName;
        sys_RtlInitUnicodeString(&usName, meta->FileName.c_str());

        st = sys_NtQueryDirectoryFileEx(meta->DirHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation,
                                        Length, FileFullDirectoryInformation, QueryFlags, &usName);
        if (NT_SUCCESS(st))
        {
            auto oldInformation = IoStatusBlock->Information;
            FixNameInfo(meta, IoStatusBlock, FileInformation);
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

static NTSTATUS Hook_NtQueryDirectoryFileEx(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                            PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                            ULONG Length, FILE_INFORMATION_CLASS FileInformationClass, ULONG QueryFlags,
                                            PUNICODE_STRING FileName)
{
    logger.Log(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length, FileInformationClass,
               QueryFlags, FileName);

    if (FileInformationClass == FileFullDirectoryInformation)
    {
        return NtQueryDirectoryFileFullDirectoryInformation(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
                                                            FileInformation, Length, QueryFlags, FileName);
    }

    return sys_NtQueryDirectoryFileEx(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, FileInformation, Length,
                                      FileInformationClass, QueryFlags, FileName);
}

static void LoadNtQueryDirectoryFileEx()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtQueryDirectoryFileEx");
    sys_NtQueryDirectoryFileEx = reinterpret_cast<T_NtQueryDirectoryFileEx>(addr);
}

appbox::HookRecord appbox::HookNtQueryDirectoryFileEx = {
    "NtQueryDirectoryFileEx",
    LoadNtQueryDirectoryFileEx,
    (void**)&sys_NtQueryDirectoryFileEx,
    Hook_NtQueryDirectoryFileEx,
};
