#include "utils/WinAPI.h" /* Must be first include file */
#include "filesystem/CreateDirectory.hpp"
#include "filesystem/DirName.hpp"
#include "filesystem/IsolationPolicy.hpp"
#include "filesystem/Resolve.hpp"
#include "filesystem/RemoveAll.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtQueryObject.hpp"
#include "hook/NtQueryFullAttributesFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "utils/BitParser.hpp"
#include "utils/CopyFileNt.hpp"
#include "utils/HandleInfo.hpp"
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "utils/Defines.hpp"
#include "utils/QueryHandlePath.hpp"
#include "utils/ConvertToFullNtPath.hpp"
#include "WString.hpp"
#include "Sandbox.hpp"
#include <vector>

enum class PathKind
{
    NotExist,
    Directory,
    File,
    Error // other failure (access denied, etc.); treated as not-traversable
};

T_NtCreateFile sys_NtCreateFile = nullptr;

static const appbox::BitData CreateDispositionMap[] = {
    { "FILE_OVERWRITE_IF", FILE_OVERWRITE_IF },
    { "FILE_OPEN_IF",      FILE_OPEN_IF      },
    { "FILE_CREATE",       FILE_CREATE       },
    { "FILE_OVERWRITE",    FILE_OVERWRITE    },
    { "FILE_OPEN",         FILE_OPEN         },
    { "FILE_SUPERSEDE",    FILE_SUPERSEDE    },
};

static const appbox::BitData CreateOptionsMap[] = {
    { "FILE_DIRECTORY_FILE",            FILE_DIRECTORY_FILE            },
    { "FILE_NON_DIRECTORY_FILE",        FILE_NON_DIRECTORY_FILE        },
    { "FILE_WRITE_THROUGH",             FILE_WRITE_THROUGH             },
    { "FILE_SEQUENTIAL_ONLY",           FILE_SEQUENTIAL_ONLY           },
    { "FILE_RANDOM_ACCESS",             FILE_RANDOM_ACCESS             },
    { "FILE_NO_INTERMEDIATE_BUFFERING", FILE_NO_INTERMEDIATE_BUFFERING },
    { "FILE_SYNCHRONOUS_IO_ALERT",      FILE_SYNCHRONOUS_IO_ALERT      },
    { "FILE_SYNCHRONOUS_IO_NONALERT",   FILE_SYNCHRONOUS_IO_NONALERT   },
    { "FILE_CREATE_TREE_CONNECTION",    FILE_CREATE_TREE_CONNECTION    },
    { "FILE_NO_EA_KNOWLEDGE",           FILE_NO_EA_KNOWLEDGE           },
    { "FILE_OPEN_REPARSE_POINT",        FILE_OPEN_REPARSE_POINT        },
    { "FILE_DELETE_ON_CLOSE",           FILE_DELETE_ON_CLOSE           },
    { "FILE_OPEN_BY_FILE_ID",           FILE_OPEN_BY_FILE_ID           },
    { "FILE_OPEN_FOR_BACKUP_INTENT",    FILE_OPEN_FOR_BACKUP_INTENT    },
    { "FILE_RESERVE_OPFILTER",          FILE_RESERVE_OPFILTER          },
    { "FILE_OPEN_REQUIRING_OPLOCK",     FILE_OPEN_REQUIRING_OPLOCK     },
    { "FILE_COMPLETE_IF_OPLOCKED",      FILE_COMPLETE_IF_OPLOCKED      },
};

static const appbox::BitData FileAttributesMap[] = {
    { "FILE_ATTRIBUTE_READONLY",              FILE_ATTRIBUTE_READONLY              },
    { "FILE_ATTRIBUTE_HIDDEN",                FILE_ATTRIBUTE_HIDDEN                },
    { "FILE_ATTRIBUTE_SYSTEM",                FILE_ATTRIBUTE_SYSTEM                },
    { "FILE_ATTRIBUTE_DIRECTORY",             FILE_ATTRIBUTE_DIRECTORY             },
    { "FILE_ATTRIBUTE_ARCHIVE",               FILE_ATTRIBUTE_ARCHIVE               },
    { "FILE_ATTRIBUTE_DEVICE",                FILE_ATTRIBUTE_DEVICE                },
    { "FILE_ATTRIBUTE_NORMAL",                FILE_ATTRIBUTE_NORMAL                },
    { "FILE_ATTRIBUTE_TEMPORARY",             FILE_ATTRIBUTE_TEMPORARY             },
    { "FILE_ATTRIBUTE_SPARSE_FILE",           FILE_ATTRIBUTE_SPARSE_FILE           },
    { "FILE_ATTRIBUTE_REPARSE_POINT",         FILE_ATTRIBUTE_REPARSE_POINT         },
    { "FILE_ATTRIBUTE_COMPRESSED",            FILE_ATTRIBUTE_COMPRESSED            },
    { "FILE_ATTRIBUTE_OFFLINE",               FILE_ATTRIBUTE_OFFLINE               },
    { "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED",   FILE_ATTRIBUTE_NOT_CONTENT_INDEXED   },
    { "FILE_ATTRIBUTE_ENCRYPTED",             FILE_ATTRIBUTE_ENCRYPTED             },
    { "FILE_ATTRIBUTE_INTEGRITY_STREAM",      FILE_ATTRIBUTE_INTEGRITY_STREAM      },
    { "FILE_ATTRIBUTE_VIRTUAL",               FILE_ATTRIBUTE_VIRTUAL               },
    { "FILE_ATTRIBUTE_NO_SCRUB_DATA",         FILE_ATTRIBUTE_NO_SCRUB_DATA         },
    { "FILE_ATTRIBUTE_EA",                    FILE_ATTRIBUTE_EA                    },
    { "FILE_ATTRIBUTE_PINNED",                FILE_ATTRIBUTE_PINNED                },
    { "FILE_ATTRIBUTE_UNPINNED",              FILE_ATTRIBUTE_UNPINNED              },
    { "FILE_ATTRIBUTE_RECALL_ON_OPEN",        FILE_ATTRIBUTE_RECALL_ON_OPEN        },
    { "FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS", FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS },
};

static const appbox::BitData ObjectAttributesMap[] = {
    { "OBJ_CASE_INSENSITIVE", OBJ_CASE_INSENSITIVE },
    { "OBJ_INHERIT",          OBJ_INHERIT          },
};

static nlohmann::json NtCreateFileLogParam(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                                           POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock,
                                           PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
                                           ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    nlohmann::json json;
    json["FileHandle"] = appbox::PointerToString(FileHandle);
    json["DesiredAccess"] = appbox::DesiredAccessToJson(DesiredAccess);
    json["ObjectAttributes"] = appbox::ToJson(ObjectAttributes, CreateOptions);
    json["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    if (AllocationSize != nullptr)
    {
        json["AllocationSize"] = AllocationSize->QuadPart;
    }
    json["FileAttributes"] = appbox::ParseBit(FileAttributes, FileAttributesMap, std::size(FileAttributesMap));
    json["ShareAccess"] = ShareAccess;
    json["CreateDisposition"] =
        appbox::ParseBit(CreateDisposition, CreateDispositionMap, std::size(CreateDispositionMap));
    json["CreateOptions"] = appbox::CreateOptionsToJson(CreateOptions);
    json["EaBuffer"] = appbox::PointerToString(EaBuffer);
    json["EaLength"] = EaLength;

    return json;
}

static appbox::LoggerF logger("NtCreateFile", NtCreateFileLogParam);

static NTSTATUS NtCreateFileOpenFS(const std::wstring& path, ULONG Attributes, PHANDLE FileHandle,
                                   ACCESS_MASK DesiredAccess, PIO_STATUS_BLOCK IoStatusBlock,
                                   PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
                                   ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength)
{
    HANDLE tmpHandle = nullptr;
    if (FileHandle == nullptr)
    {
        FileHandle = &tmpHandle;
    }
    IO_STATUS_BLOCK tmpIoStatusBlock;
    if (IoStatusBlock == nullptr)
    {
        IoStatusBlock = &tmpIoStatusBlock;
    }

    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING    usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);
    auto st = sys_NtCreateFile(FileHandle, DesiredAccess, &oa, IoStatusBlock, AllocationSize, FileAttributes,
                               ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    if (NT_SUCCESS(st) && FileHandle == &tmpHandle)
    {
        sys_NtClose(tmpHandle);
    }
    return st;
}

/**
 * @brief Whether the handle of the call may be marked for deletion.
 *
 * A delete on close is a modification of the entry, so the caller has to ask
 * for the access which removes it: `FILE_DELETE_ON_CLOSE` is only accepted
 * together with `DELETE`, and a caller which marks the handle for deletion
 * afterwards asks for `DELETE` as well. A handle which carries neither access
 * can never delete its object, so no handle information is needed for it.
 *
 * @param[in] DesiredAccess Access the caller asked for.
 * @param[in] CreateOptions Options of the call.
 * @return true when the handle may be marked for deletion.
 */
static bool MayDeleteOnClose(ACCESS_MASK DesiredAccess, ULONG CreateOptions)
{
    return (DesiredAccess & DELETE) != 0 || (CreateOptions & FILE_DELETE_ON_CLOSE) != 0;
}

/**
 * @brief Whether the handle of the call denotes a directory.
 *
 * The kind of the entry decides whether the merge of a directory enumeration
 * can use the handle. It is taken from the layers the call resolved to, which
 * the file system filled for every entry it holds: a call which creates the
 * entry names the kind in its options instead, because no layer holds the entry
 * while the call resolves it.
 *
 * @param[in] resolve_result Layers the call resolved to.
 * @param[in] CreateOptions Options of the call.
 * @return true when the handle denotes a directory.
 */
static bool IsDirectoryHandle(const appbox::filesystem::ResolveResult& resolve_result, ULONG CreateOptions)
{
    if ((CreateOptions & FILE_DIRECTORY_FILE) != 0)
    {
        return true;
    }

    return !resolve_result.hPath.empty() &&
           (resolve_result.hPath[0].fInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/**
 * @brief Whether the handle of the call has to be recorded.
 *
 * `NtClose` records the delete of a handle which carries a pending delete and
 * the directory entry points merge the layers of a directory handle, so both
 * need the record `NtOpenFile` writes for every handle it opens: a handle which
 * may be marked for deletion and a handle which denotes a directory are
 * recorded here as well. A directory is recorded whatever access the caller
 * asked for, because the access a call asks for is a generic right of the
 * caller which the file system maps afterwards.
 *
 * @param[in] resolve_result Layers the call resolved to.
 * @param[in] DesiredAccess Access the caller asked for.
 * @param[in] CreateOptions Options of the call.
 * @return true when the call has to record the handle.
 */
static bool NeedsHandleRecord(const appbox::filesystem::ResolveResult& resolve_result, ACCESS_MASK DesiredAccess,
                              ULONG CreateOptions)
{
    return MayDeleteOnClose(DesiredAccess, CreateOptions) || IsDirectoryHandle(resolve_result, CreateOptions);
}

/**
 * @brief Record the information of a handle the sandbox opened.
 *
 * `NtClose` is where the sandbox records the delete of a handle which carries
 * a pending delete, so such a handle needs the information `NtOpenFile` writes
 * as well. The recorded resolve has to describe the layers as they are once
 * the open succeeded: the close removes the object the handle denotes, so the
 * record is what tells the delete which layers still hold the name, and it is
 * the only place that information is available: the object is gone by the time
 * the close runs.
 *
 * A record of a directory carries every layer which holds it, because the
 * merge of a directory enumeration walks them one after the other, which is
 * what the resolver is asked for all of them here.
 *
 * The path is resolved again here instead of reusing the result of the call,
 * because the call changes what the layers hold: it creates the entry, copies
 * it up into the upper layer and removes a whiteout marker which hid the entry
 * of a lower layer, and a record taken before the call would name the wrong
 * layers. A path the record cannot be built for stays unrecorded, in which
 * case the close removes the layer object without hiding the layers below it
 * and an enumeration reports the layer of the handle.
 *
 * @param[in] handle Handle the call opened.
 * @param[in] viewPath Path of the view the handle denotes.
 * @param[in] ObjectAttributes Attributes of the call.
 * @param[in] CreateOptions Options of the call.
 */
static void RecordHandle(HANDLE handle, const std::wstring& viewPath, POBJECT_ATTRIBUTES ObjectAttributes,
                         ULONG CreateOptions)
{
    appbox::filesystem::ResolveOption resolve_option;
    resolve_option.NameAttributes = ObjectAttributes->Attributes;
    resolve_option.bStopOnFirstFound = false;

    auto resolve_result = appbox::filesystem::Resolve(viewPath, resolve_option);
    LOG_T("resolve for handle: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));
    if (resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        return;
    }

    appbox::HandleInfo::Create(
        handle, [&viewPath, ObjectAttributes, &resolve_result, CreateOptions](appbox::HandleInfo::Ptr info) {
            info->viewPath = viewPath;
            info->resolve = resolve_result;
            info->ObjAttributes = ObjectAttributes->Attributes;
            info->bDeleteOnClose = (CreateOptions & FILE_DELETE_ON_CLOSE) != 0;
        });
}

static NTSTATUS Hook_NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                  PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
                                  ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer,
                                  ULONG EaLength)
{
    logger.Log(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
               CreateDisposition, CreateOptions, EaBuffer, EaLength);

    if (appbox::ThreadLocal::Get().disable_NtCreateFile_hook)
    {
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    /* Get file path in sandbox */
    std::wstring nativate_fs_nt_path;
    if (appbox::ConvertToFullNtPath(ObjectAttributes, CreateOptions, nativate_fs_nt_path) != 0)
    {
        LOG_D("ConvertToFullNtPath failed");
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }

    std::wstring nativate_fs_path;
    if (!appbox::MappingAsDosNtPath(nativate_fs_nt_path, nativate_fs_path))
    {
        LOG_D(L"MappingAsDosNtPath failed: {}", nativate_fs_nt_path);
        return sys_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
                                FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
    }
    CreateOptions &= ~FILE_OPEN_BY_FILE_ID;

    const bool want_create = (CreateDisposition == FILE_SUPERSEDE || CreateDisposition == FILE_CREATE ||
                              CreateDisposition == FILE_OPEN_IF || CreateDisposition == FILE_OVERWRITE_IF);
    const bool want_edit = (DesiredAccess & (DELETE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA |
                                             FILE_APPEND_DATA | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL));

    /*
     * A call which may create or write the entry is a modification, and the
     * layer it lands in is decided from the layers which hold the entry, so
     * such a call asks the resolver for every layer. The isolation of a path
     * no entry covers is the default of the view, which is `Merge`, so the
     * resolver has to know every layer here as well.
     */
    appbox::filesystem::ResolveOption resolve_option;
    resolve_option.bStopOnFirstFound = !(want_create || want_edit);

    /* Resolve path in sandbox. */
    auto resolve_result = appbox::filesystem::Resolve(nativate_fs_path, resolve_option);
    LOG_T("resolve: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));
    /* In all of conditions, the parent path must exist. */
    if (!resolve_result->bParentExist)
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    /* Check CreateDisposition */
    if (CreateDisposition == FILE_CREATE && resolve_result->status == appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_COLLISION;
    }
    if (CreateDisposition == FILE_OVERWRITE &&
        resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /*
     * An entry which the isolation hides does not exist in the view: a call
     * which does not ask for a creation reports the same failure as a missing
     * file instead of a missing path, because the parent directory of the
     * entry is hidden as well. A creation is allowed and lands in the upper
     * layer, which is what makes the entry visible from then on.
     */
    if (!want_create && resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists &&
        (resolve_result->status == appbox::filesystem::ResolveResult::Status::HiddenByIsolation ||
         resolve_result->bIsolationMasked))
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /* If file is hidden by whiteout, remove the whiteout file */
    if (want_create && resolve_result->status == appbox::filesystem::ResolveResult::Status::HiddenByWhiteout &&
        resolve_result->bWhiteoutInUpper)
    {
        appbox::filesystem::RemoveAll(resolve_result->whiteoutPath, ObjectAttributes->Attributes);

        /*
         * The whiteout hid every layer below the upper one, so the layers
         * which hold the entry have to be looked up again before the layer of
         * the creation is picked: `Merge` writes to the host filesystem when
         * the host holds the entry or when no layer holds it, and the entry
         * the whiteout hid may be a packed entry which only a lower layer
         * holds. A path no entry covers follows the default of the view, which
         * is `Merge` as well, so the mode of the result decides on its own.
         */
        if (resolve_result->isolation == appbox::FilesystemIsolation::Merge)
        {
            resolve_result = appbox::filesystem::Resolve(nativate_fs_path, resolve_option);
            LOG_T("resolve after whiteout: {}", appbox::DumpJson(nlohmann::json(*resolve_result)));
        }

        /* If want to create directory, search again to check if we need to create opaque file */
        if (CreateOptions & FILE_DIRECTORY_FILE)
        {
            auto rResult = appbox::filesystem::Resolve(nativate_fs_path);
            if (rResult->status == appbox::filesystem::ResolveResult::Status::Exists)
            {
                /*
                 * The directory still exists in a lower layer: it is created
                 * in the upper layer and an opaque marker hides the content of
                 * the lower layer, so the re-created directory starts empty.
                 */
                NtCreateFileOpenFS(resolve_result->uPath, ObjectAttributes->Attributes, nullptr, DesiredAccess,
                                   IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition,
                                   CreateOptions, EaBuffer, EaLength);
                return NtCreateFileOpenFS(resolve_result->uPath + L"\\" + APPBOX_SANDBOX_OPAQUE_NAME_W,
                                          ObjectAttributes->Attributes, nullptr, DELETE | FILE_WRITE_DATA, nullptr,
                                          nullptr, FILE_ATTRIBUTE_NORMAL,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN_IF,
                                          FILE_NON_DIRECTORY_FILE, nullptr, 0);
            }
        }
    }

    /*
     * The layer the call is applied to. `Merge` writes to the host filesystem
     * whenever the host holds the entry or when no layer holds it at all,
     * while every other mode and every entry only the sandbox holds stays in
     * the upper layer. A call which neither creates nor writes the entry is no
     * modification, so it keeps reading the layer the view prefers.
     */
    const bool modifies =
        want_edit || (want_create && resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists);
    const bool target_host =
        modifies && appbox::filesystem::WritesToHost(resolve_result->isolation, resolve_result->bHostHolds,
                                                     resolve_result->bSandboxHolds);
    LOG_T(L"create: target={}", target_host ? L"host" : L"view");

    if (want_create || want_edit)
    {
        /*
         * The folders above the entry have to exist in the layer the entry is
         * created in. The call which creates them uses the original entry
         * point of the process, so a folder of the host filesystem is created
         * without being redirected into the view again.
         */
        const std::wstring& parent = target_host ? resolve_result->hostPath : resolve_result->uPath;
        const size_t parent_base = target_host ? resolve_result->hostPathBaseSize : resolve_result->uPathBaseSize;
        appbox::filesystem::CreateDirectories(appbox::filesystem::DirName(parent), parent_base);
    }
    if (want_edit && !target_host && resolve_result->status == appbox::filesystem::ResolveResult::Status::Exists &&
        !resolve_result->bInUpper)
    {
        appbox::CopyFileNt(resolve_result->hPath[0].fPath, resolve_result->uPath);
    }

    std::wstring open_path;
    if (target_host)
    {
        open_path = resolve_result->hostPath;
    }
    else if (want_edit || resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        open_path = resolve_result->uPath;
    }
    else
    {
        open_path = resolve_result->hPath[0].fPath;
    }

    const NTSTATUS st = NtCreateFileOpenFS(open_path, ObjectAttributes->Attributes, FileHandle, DesiredAccess,
                                           IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
                                           CreateDisposition, CreateOptions, EaBuffer, EaLength);

    /*
     * A handle which the caller may mark for deletion or which denotes a
     * directory records the same information `NtOpenFile` records, so the
     * close of the handle can hide the layers which still hold the name and the
     * entry points which enumerate a directory can merge the layers which hold
     * it. The handle is only known once the call succeeded, and a caller which
     * passed no handle keeps the internal one of the helper, which the helper
     * closed already.
     */
    if (NT_SUCCESS(st) && FileHandle != nullptr && NeedsHandleRecord(*resolve_result, DesiredAccess, CreateOptions))
    {
        RecordHandle(*FileHandle, nativate_fs_path, ObjectAttributes, CreateOptions);
    }

    return st;
}

static void LoadNtCreateFile()
{
    auto addr = GetProcAddress(appbox::sys.h_ntdll, "NtCreateFile");
    sys_NtCreateFile = reinterpret_cast<T_NtCreateFile>(addr);
}

appbox::NtCreateFileLock::NtCreateFileLock()
{
    appbox::ThreadLocal::Get().disable_NtCreateFile_hook = true;
}

appbox::NtCreateFileLock::~NtCreateFileLock()
{
    appbox::ThreadLocal::Get().disable_NtCreateFile_hook = false;
}

nlohmann::json appbox::ToJson(const POBJECT_ATTRIBUTES ObjectAttributes, ULONG CreateOptions)
{
    if (ObjectAttributes == nullptr)
    {
        return nullptr;
    }

    nlohmann::json json;
    json["Length"] = ObjectAttributes->Length;
    json["RootDirectory"] = appbox::PointerToString(ObjectAttributes->RootDirectory);
    if (ObjectAttributes->ObjectName != nullptr)
    {
        nlohmann::json name;
        name["Length"] = ObjectAttributes->ObjectName->Length;
        name["MaximumLength"] = ObjectAttributes->ObjectName->MaximumLength;

        if ((CreateOptions & FILE_OPEN_BY_FILE_ID) != 0)
        {
            /*
             * The name of such a call is an identifier, not a string. It is
             * only read when the caller provided a buffer of the right size.
             */
            if (ObjectAttributes->ObjectName->Buffer != nullptr &&
                ObjectAttributes->ObjectName->MaximumLength >= sizeof(uint64_t))
            {
                auto p_file_id = reinterpret_cast<uint64_t*>(ObjectAttributes->ObjectName->Buffer);
                name["Buffer"] = *p_file_id;
            }
            else
            {
                name["Buffer"] = "";
            }
        }
        else
        {
            name["Buffer"] = appbox::UnicodeStringToUTF8(ObjectAttributes->ObjectName);
        }

        json["ObjectName"] = name;
    }
    json["Attributes"] =
        appbox::ParseBit(ObjectAttributes->Attributes, ObjectAttributesMap, std::size(ObjectAttributesMap));
    json["SecurityDescriptor"] = appbox::PointerToString(ObjectAttributes->SecurityDescriptor);
    json["SecurityQualityOfService"] = appbox::PointerToString(ObjectAttributes->SecurityQualityOfService);

    return json;
}

nlohmann::json appbox::CreateOptionsToJson(ULONG CreateOptions)
{
    return appbox::ParseBit(CreateOptions, CreateOptionsMap, std::size(CreateOptionsMap));
}

appbox::HookRecord appbox::HookNtCreateFile = {
    "NtCreateFile",
    LoadNtCreateFile,
    (void**)&sys_NtCreateFile,
    Hook_NtCreateFile,
};
